//
// Created by fish on 2025/9/24.
//

#include "bsp/can.h"

#include <string.h>

#include "fdcan.h"
#include "bsp/sys.h"

static bsp_can_handle_t *handle[BSP_CAN_DEVICE_COUNT] = {
    [E_CAN_1] = &hfdcan1,
    [E_CAN_2] = &hfdcan2,
    [E_CAN_3] = &hfdcan3
};

static uint8_t cnt[BSP_CAN_DEVICE_COUNT];
static uint32_t filter_id[BSP_CAN_DEVICE_COUNT][BSP_CAN_FILTER_LIMIT_STD];
static uint32_t filter_mask[BSP_CAN_DEVICE_COUNT][BSP_CAN_FILTER_LIMIT_STD];
static bsp_can_callback_t callback[BSP_CAN_DEVICE_COUNT][BSP_CAN_FILTER_LIMIT_STD];
static volatile bsp_can_stats_t stats[BSP_CAN_DEVICE_COUNT];

static uint8_t rx_buffer[BSP_CAN_DEVICE_COUNT][BSP_CAN_FILTER_LIMIT_STD][BSP_CAN_BUFFER_SIZE];

bsp_status_t bsp_can_init(bsp_can_e device) {
    BSP_ASSERT(0 <= device && device < BSP_CAN_DEVICE_COUNT);
    HAL_StatusTypeDef hal = HAL_FDCAN_ActivateNotification(
        handle[device],
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
        FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
        FDCAN_IT_BUS_OFF,
        0
    );
    if (hal == HAL_OK) hal = HAL_FDCAN_ConfigGlobalFilter(handle[device], FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    if (hal == HAL_OK) hal = HAL_FDCAN_Start(handle[device]);
    const bsp_status_t status = bsp_status_from_hal(hal);
    stats[device].last_status = status;
    return status;
}

bsp_status_t bsp_can_set_callback(bsp_can_e device, uint32_t id, bsp_can_callback_t func) {
    return bsp_can_set_mask_callback(device, id, 0x7ff, func);
}

bsp_status_t bsp_can_set_mask_callback(
    bsp_can_e device,
    uint32_t id,
    uint32_t mask,
    bsp_can_callback_t func
) {
    if (device < 0 || device >= BSP_CAN_DEVICE_COUNT || id > 0x7ff ||
        mask > 0x7ff || func == NULL) {
        return BSP_STATUS_ERROR;
    }

    const unsigned long state = bsp_sys_enter_critical();
    for (uint8_t i = 0; i < cnt[device]; i++) {
        if (filter_id[device][i] == id && filter_mask[device][i] == mask) {
            bsp_sys_exit_critical(state);
            return BSP_STATUS_BUSY;
        }
    }
    if (cnt[device] >= BSP_CAN_FILTER_LIMIT_STD) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_ERROR;
    }

    const uint8_t index = cnt[device];

    FDCAN_FilterTypeDef filter = {
        .IdType = FDCAN_STANDARD_ID,
        .FilterIndex = index,
        .FilterType = FDCAN_FILTER_MASK,
        .FilterID1 = id,
        .FilterID2 = mask,
        .FilterConfig = FDCAN_FILTER_TO_RXFIFO0,
    };

    const bsp_status_t status = bsp_status_from_hal(HAL_FDCAN_ConfigFilter(handle[device], &filter));
    stats[device].last_status = status;
    if (status == BSP_STATUS_OK) {
        filter_id[device][index] = id;
        filter_mask[device][index] = mask;
        callback[device][index] = func;
        cnt[device]++;
    }
    bsp_sys_exit_critical(state);
    return status;
}

static uint32_t len2code(uint8_t l) {
    if(l <= 8) return l;
    if(l == 12) return FDCAN_DLC_BYTES_12;
    if(l == 16) return FDCAN_DLC_BYTES_16;
    if(l == 20) return FDCAN_DLC_BYTES_20;
    if(l == 24) return FDCAN_DLC_BYTES_24;
    if(l == 32) return FDCAN_DLC_BYTES_32;
    if(l == 48) return FDCAN_DLC_BYTES_48;
    if(l == 64) return FDCAN_DLC_BYTES_64;
    return UINT32_MAX;
}

// len <= 8 时使用标准 can，len > 8 时使用 fdcan
// **若使用 fdcan，总线上不能有只支持标准 can 的节点
bsp_status_t bsp_can_send(bsp_can_e device, uint32_t id, const uint8_t *data, uint8_t len) {
    if (device < 0 || device >= BSP_CAN_DEVICE_COUNT || data == NULL || len == 0 || len > 64 || id > 0x7ff) return BSP_STATUS_ERROR;
    const uint32_t dlc = len2code(len);
    if (dlc == UINT32_MAX) return BSP_STATUS_ERROR;
    FDCAN_TxHeaderTypeDef header = {
        .Identifier = id,
        .IdType = FDCAN_STANDARD_ID,
        .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = dlc,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch = len > 8 ? FDCAN_BRS_ON : FDCAN_BRS_OFF,
        .FDFormat = len > 8 ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN,
        .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
        .MessageMarker = 0x01
    };

    const unsigned long state = bsp_sys_enter_critical();
    const bsp_status_t status = bsp_status_from_hal(HAL_FDCAN_AddMessageToTxFifoQ(handle[device], &header, data));
    stats[device].last_status = status;
    if (status != BSP_STATUS_OK) {
        stats[device].tx_error_count++;
    }
    bsp_sys_exit_critical(state);
    return status;
}

static uint8_t code2len(uint32_t l) {
    if(l <= FDCAN_DLC_BYTES_8) return (uint8_t) l;
    if(l == FDCAN_DLC_BYTES_12) return 12;
    if(l == FDCAN_DLC_BYTES_16) return 16;
    if(l == FDCAN_DLC_BYTES_20) return 20;
    if(l == FDCAN_DLC_BYTES_24) return 24;
    if(l == FDCAN_DLC_BYTES_32) return 32;
    if(l == FDCAN_DLC_BYTES_48) return 48;
    if(l == FDCAN_DLC_BYTES_64) return 64;
    return UINT8_MAX;
}

void bsp_can_callback_sol(bsp_can_e device, uint32_t fifo) {
    FDCAN_RxHeaderTypeDef header;
    static uint8_t buf[BSP_CAN_BUFFER_SIZE] = { 0 };
    while (HAL_FDCAN_GetRxFifoFillLevel(handle[device], fifo)) {
        if (HAL_FDCAN_GetRxMessage(handle[device], fifo, &header, buf) != HAL_OK) {
            stats[device].rx_error_count++;
            stats[device].last_status = BSP_STATUS_ERROR;
            break;
        }
        if (header.FilterIndex >= cnt[device]) continue;

        const uint8_t len = code2len(header.DataLength);
        if (len == UINT8_MAX) {
            stats[device].rx_error_count++;
            stats[device].last_status = BSP_STATUS_ERROR;
            continue;
        }

        const uint8_t index = (uint8_t) header.FilterIndex;
        memcpy(rx_buffer[device][index], buf, len);
        callback[device][index](device, header.Identifier, rx_buffer[device][index], len);
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *device, uint32_t RxFifo0ITs) {
    bsp_can_e index;
    switch((uint32_t) device->Instance) {
    case (uint32_t) FDCAN1:
        index = E_CAN_1; break;
    case (uint32_t) FDCAN2:
        index = E_CAN_2; break;
    case (uint32_t) FDCAN3:
        index = E_CAN_3; break;
    default: return;
    }
    if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) {
        stats[index].rx_fifo_lost_count++;
    }
    bsp_can_callback_sol(index, FDCAN_RX_FIFO0);
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *device, uint32_t itflags) {
    if (itflags & FDCAN_IT_BUS_OFF) {
        bsp_can_e index;
        if (device->Instance == FDCAN1) index = E_CAN_1;
        else if (device->Instance == FDCAN2) index = E_CAN_2;
        else if (device->Instance == FDCAN3) index = E_CAN_3;
        else return;
        stats[index].bus_off_count++;
        stats[index].last_status = BSP_STATUS_OFFLINE;
    }
}

bsp_can_stats_t bsp_can_stats(bsp_can_e device) {
    BSP_ASSERT(0 <= device && device < BSP_CAN_DEVICE_COUNT);
    const unsigned long state = bsp_sys_enter_critical();
    const bsp_can_stats_t copy = {
        .tx_error_count = stats[device].tx_error_count,
        .rx_error_count = stats[device].rx_error_count,
        .rx_fifo_lost_count = stats[device].rx_fifo_lost_count,
        .bus_off_count = stats[device].bus_off_count,
        .last_status = stats[device].last_status,
    };
    bsp_sys_exit_critical(state);
    return copy;
}
