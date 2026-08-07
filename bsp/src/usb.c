#include "bsp/usb.h"

#include "bsp/ds.h"
#include "bsp/sys.h"

#include "stm32h7xx_hal.h"
#include "usbd_cdc_acm.h"
#include "usbd_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USB_BUS_ID 0U

#define CDC_IN_EP  0x81U
#define CDC_OUT_EP 0x02U
#define CDC_INT_EP 0x83U

#define WINUSB_IN_EP  0x84U
#define WINUSB_OUT_EP 0x05U

#define CDC_MAX_PACKET_SIZE 64U
#define WINUSB_MAX_PACKET_SIZE 64U
#define WINUSB_VENDOR_CODE     0x17U

#define WINUSB_DESCRIPTOR_SIZE (9U + 7U + 7U)
#define USB_CONFIG_SIZE        (9U + CDC_ACM_DESCRIPTOR_LEN + WINUSB_DESCRIPTOR_SIZE)
#define CDC_TX_QUEUE_SIZE   (BSP_USB_CDC_BUFFER_SIZE * 4U)

typedef enum {
    USB_TX_IDLE,
    USB_TX_DATA,
    USB_TX_ZLP,
} usb_tx_state_t;

static const uint8_t ms_os_20_descriptor[] = {
    USB_MSOSV2_COMP_ID_SET_HEADER_DESCRIPTOR_INIT(
        10U + USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_MULTI_DESCRIPTOR_LEN
    ),
    USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_MULTI_DESCRIPTOR_INIT(0x02),
};

static USB_MEM_ALIGNX const uint8_t bos_descriptor_data[] = {
    USB_BOS_HEADER_DESCRIPTOR_INIT(5U + USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_LEN, 1U),
    USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_INIT(
        WINUSB_VENDOR_CODE, sizeof(ms_os_20_descriptor)
    ),
};

static const struct usb_msosv2_descriptor ms_os_20 = {
    .vendor_code = WINUSB_VENDOR_CODE,
    .compat_id = ms_os_20_descriptor,
    .compat_id_len = sizeof(ms_os_20_descriptor),
};

static const struct usb_bos_descriptor bos_descriptor = {
    .string = bos_descriptor_data,
    .string_len = sizeof(bos_descriptor_data),
};

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, 0x0483, 0x5741, 0x0101, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, 100),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_PACKET_SIZE, 0x00),
    USB_INTERFACE_DESCRIPTOR_INIT(0x02, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x04),
    USB_ENDPOINT_DESCRIPTOR_INIT(
        WINUSB_OUT_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_MAX_PACKET_SIZE, 0x00
    ),
    USB_ENDPOINT_DESCRIPTOR_INIT(
        WINUSB_IN_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_MAX_PACKET_SIZE, 0x00
    ),
};

static char serial_number[14];
static const char language_id[] = { 0x09, 0x04 };

static const uint8_t *device_descriptor_callback(uint8_t speed) {
    (void) speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed) {
    (void) speed;
    return config_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index) {
    (void) speed;
    switch (index) {
        case 0: return language_id;
        case 1: return "trobot";
        case 2: return "trobot cdc + debug";
        case 3: return serial_number;
        case 4: return "trobot debug";
        default: return NULL;
    }
}

static const struct usb_descriptor usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv2_descriptor = &ms_os_20,
    .bos_descriptor = &bos_descriptor,
};

static void cdc_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void cdc_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void winusb_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void winusb_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);

static struct usbd_endpoint cdc_out_endpoint = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = cdc_out_callback,
};

static struct usbd_endpoint cdc_in_endpoint = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = cdc_in_callback,
};

static struct usbd_endpoint winusb_out_endpoint = {
    .ep_addr = WINUSB_OUT_EP,
    .ep_cb = winusb_out_callback,
};

static struct usbd_endpoint winusb_in_endpoint = {
    .ep_addr = WINUSB_IN_EP,
    .ep_cb = winusb_in_callback,
};

static struct usbd_interface cdc_control_interface;
static struct usbd_interface cdc_data_interface;
static struct usbd_interface winusb_interface;

static ds_rq_t cdc_tx_queue;
static uint8_t cdc_tx_queue_buffer[CDC_TX_QUEUE_SIZE];
static USB_MEM_ALIGNX uint8_t cdc_tx_buffer[BSP_USB_CDC_BUFFER_SIZE];
static USB_MEM_ALIGNX uint8_t cdc_rx_buffer[2][CDC_MAX_PACKET_SIZE];
static USB_MEM_ALIGNX uint8_t winusb_rx_buffer[2][BSP_USB_WINUSB_REQUEST_SIZE];

static volatile usb_tx_state_t cdc_tx_state;
static volatile uint16_t cdc_tx_length;
static volatile uint8_t cdc_rx_active;
static volatile usb_tx_state_t winusb_tx_state;
static volatile uint16_t winusb_tx_length;
static volatile uint8_t winusb_rx_active;
static volatile uint8_t configured;
static volatile uint8_t suspended;
static bsp_usb_cdc_callback_t cdc_rx_callback;
static bsp_usb_winusb_callback_t winusb_rx_callback;

static struct cdc_line_coding line_coding = {
    .dwDTERate = 115200,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
};

static char hex_digit(uint32_t value) {
    value &= 0x0FU;
    return (char) (value < 10U ? '0' + value : 'A' + value - 10U);
}

static void serial_append_hex(char *destination, uint32_t value, uint8_t digits) {
    for (uint8_t i = 0; i < digits; i++) {
        destination[i] = hex_digit(value >> 28U);
        value <<= 4U;
    }
}

static void serial_number_init(void) {
    serial_append_hex(serial_number, HAL_GetUIDw0() + HAL_GetUIDw2(), 8);
    serial_append_hex(serial_number + 8, HAL_GetUIDw1(), 4);
    serial_number[12] = 'C';
    serial_number[13] = '\0';
}

static void cdc_tx_reset_locked(void) {
    ds_rq_init(&cdc_tx_queue, cdc_tx_queue_buffer, sizeof(cdc_tx_queue_buffer));
    cdc_tx_state = USB_TX_IDLE;
    cdc_tx_length = 0;
}

static void cdc_tx_start_next_locked(void) {
    uint16_t length;
    if (ds_rq_size(&cdc_tx_queue) == 0) {
        cdc_tx_state = USB_TX_IDLE;
        cdc_tx_length = 0;
        return;
    }

    ds_rq_pop(&cdc_tx_queue, (uint8_t *) &length, sizeof(length));
    ds_rq_pop(&cdc_tx_queue, cdc_tx_buffer, length);
    cdc_tx_length = length;
    cdc_tx_state = USB_TX_DATA;
    if (usbd_ep_start_write(USB_BUS_ID, CDC_IN_EP, cdc_tx_buffer, length) != 0) {
        cdc_tx_reset_locked();
    }
}

static void winusb_tx_reset_locked(void) {
    winusb_tx_state = USB_TX_IDLE;
    winusb_tx_length = 0;
}

static void usb_connection_reset(void) {
    const unsigned long state = bsp_sys_enter_critical();
    configured = 0;
    suspended = 0;
    cdc_tx_reset_locked();
    winusb_tx_reset_locked();
    bsp_sys_exit_critical(state);
}

static void usb_event_handler(uint8_t busid, uint8_t event) {
    (void) busid;

    switch (event) {
        case USBD_EVENT_CONFIGURED: {
            const unsigned long state = bsp_sys_enter_critical();
            configured = 1;
            suspended = 0;
            cdc_rx_active = 0;
            winusb_rx_active = 0;
            cdc_tx_reset_locked();
            winusb_tx_reset_locked();
            bsp_sys_exit_critical(state);

            /* Arm one packet so max-sized host writes do not require an OUT ZLP. */
            (void) usbd_ep_start_read(
                USB_BUS_ID, CDC_OUT_EP, cdc_rx_buffer[cdc_rx_active], CDC_MAX_PACKET_SIZE
            );
            (void) usbd_ep_start_read(
                USB_BUS_ID, WINUSB_OUT_EP, winusb_rx_buffer[winusb_rx_active],
                BSP_USB_WINUSB_REQUEST_SIZE
            );
            break;
        }
        case USBD_EVENT_SUSPEND: {
            const unsigned long state = bsp_sys_enter_critical();
            suspended = 1;
            bsp_sys_exit_critical(state);
            break;
        }
        case USBD_EVENT_RESUME: {
            const unsigned long state = bsp_sys_enter_critical();
            suspended = 0;
            bsp_sys_exit_critical(state);
            break;
        }
        case USBD_EVENT_ERROR:
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
        case USBD_EVENT_DEINIT:
            usb_connection_reset();
            break;
        default:
            break;
    }
}

static void cdc_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) ep;

    const uint8_t completed = cdc_rx_active;
    cdc_rx_active ^= 1U;
    (void) usbd_ep_start_read(
        USB_BUS_ID, CDC_OUT_EP, cdc_rx_buffer[cdc_rx_active], CDC_MAX_PACKET_SIZE
    );

    const unsigned long state = bsp_sys_enter_critical();
    bsp_usb_cdc_callback_t callback = cdc_rx_callback;
    bsp_sys_exit_critical(state);
    if (callback != NULL && nbytes > 0) callback(cdc_rx_buffer[completed], nbytes);
}

static void cdc_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) ep;
    (void) nbytes;

    const unsigned long state = bsp_sys_enter_critical();
    if (cdc_tx_state == USB_TX_DATA && cdc_tx_length % CDC_MAX_PACKET_SIZE == 0U) {
        cdc_tx_state = USB_TX_ZLP;
        if (usbd_ep_start_write(USB_BUS_ID, CDC_IN_EP, NULL, 0) != 0) {
            cdc_tx_reset_locked();
        }
    } else if (cdc_tx_state == USB_TX_DATA || cdc_tx_state == USB_TX_ZLP) {
        cdc_tx_start_next_locked();
    }
    bsp_sys_exit_critical(state);
}

static void winusb_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) ep;

    const uint8_t completed = winusb_rx_active;
    winusb_rx_active ^= 1U;
    (void) usbd_ep_start_read(
        USB_BUS_ID, WINUSB_OUT_EP, winusb_rx_buffer[winusb_rx_active],
        BSP_USB_WINUSB_REQUEST_SIZE
    );

    const unsigned long state = bsp_sys_enter_critical();
    bsp_usb_winusb_callback_t callback = winusb_rx_callback;
    bsp_sys_exit_critical(state);
    if (callback != NULL && nbytes > 0) callback(winusb_rx_buffer[completed], nbytes);
}

static void winusb_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) ep;
    (void) nbytes;

    const unsigned long state = bsp_sys_enter_critical();
    if (winusb_tx_state == USB_TX_DATA &&
        winusb_tx_length % WINUSB_MAX_PACKET_SIZE == 0U) {
        winusb_tx_state = USB_TX_ZLP;
        if (usbd_ep_start_write(USB_BUS_ID, WINUSB_IN_EP, NULL, 0) != 0) {
            winusb_tx_reset_locked();
        }
    } else if (winusb_tx_state == USB_TX_DATA || winusb_tx_state == USB_TX_ZLP) {
        winusb_tx_reset_locked();
    }
    bsp_sys_exit_critical(state);
}

bsp_status_t bsp_usb_init(void) {
    cdc_tx_reset_locked();
    winusb_tx_reset_locked();

    serial_number_init();
    usbd_desc_register(USB_BUS_ID, &usb_descriptor);
    usbd_add_interface(USB_BUS_ID, usbd_cdc_acm_init_intf(USB_BUS_ID, &cdc_control_interface));
    usbd_add_interface(USB_BUS_ID, usbd_cdc_acm_init_intf(USB_BUS_ID, &cdc_data_interface));
    usbd_add_endpoint(USB_BUS_ID, &cdc_out_endpoint);
    usbd_add_endpoint(USB_BUS_ID, &cdc_in_endpoint);
    usbd_add_interface(USB_BUS_ID, &winusb_interface);
    usbd_add_endpoint(USB_BUS_ID, &winusb_out_endpoint);
    usbd_add_endpoint(USB_BUS_ID, &winusb_in_endpoint);

    return usbd_initialize(USB_BUS_ID, USB_OTG_HS_PERIPH_BASE, usb_event_handler) == 0
        ? BSP_STATUS_OK
        : BSP_STATUS_ERROR;
}

bool bsp_usb_is_configured(void) {
    const unsigned long state = bsp_sys_enter_critical();
    const bool result = configured != 0;
    bsp_sys_exit_critical(state);
    return result;
}

bool bsp_usb_winusb_tx_busy(void) {
    const unsigned long state = bsp_sys_enter_critical();
    const bool result = winusb_tx_state != USB_TX_IDLE;
    bsp_sys_exit_critical(state);
    return result;
}

bsp_status_t bsp_usb_cdc_send(const uint8_t *data, uint16_t len) {
    if (data == NULL || len == 0 || len > BSP_USB_CDC_BUFFER_SIZE) return BSP_STATUS_ERROR;

    const unsigned long state = bsp_sys_enter_critical();
    if (!configured || suspended) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_OFFLINE;
    }

    if (cdc_tx_state != USB_TX_IDLE) {
        if (ds_rq_avail(&cdc_tx_queue) < (int) (sizeof(len) + len)) {
            bsp_sys_exit_critical(state);
            return BSP_STATUS_BUSY;
        }
        ds_rq_push(&cdc_tx_queue, (const uint8_t *) &len, sizeof(len));
        ds_rq_push(&cdc_tx_queue, data, len);
        bsp_sys_exit_critical(state);
        return BSP_STATUS_OK;
    }

    if (data != cdc_tx_buffer) memcpy(cdc_tx_buffer, data, len);
    cdc_tx_length = len;
    cdc_tx_state = USB_TX_DATA;
    if (usbd_ep_start_write(USB_BUS_ID, CDC_IN_EP, cdc_tx_buffer, len) != 0) {
        cdc_tx_reset_locked();
        bsp_sys_exit_critical(state);
        return BSP_STATUS_ERROR;
    }
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

bsp_status_t bsp_usb_cdc_printf(const char *fmt, ...) {
    if (fmt == NULL) return BSP_STATUS_ERROR;

    va_list args;
    va_start(args, fmt);
    uint8_t buffer[BSP_USB_CDC_BUFFER_SIZE];
    const int len = vsnprintf((char *) buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len <= 0) return BSP_STATUS_ERROR;

    const uint16_t send_len =
        len < (int) sizeof(buffer) ? (uint16_t) len : (uint16_t) sizeof(buffer) - 1U;
    return bsp_usb_cdc_send(buffer, send_len);
}

bsp_status_t bsp_usb_cdc_set_callback(bsp_usb_cdc_callback_t callback) {
    if (callback == NULL) return BSP_STATUS_ERROR;

    const unsigned long state = bsp_sys_enter_critical();
    cdc_rx_callback = callback;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

bsp_status_t bsp_usb_winusb_send(const uint8_t *data, uint16_t len) {
    if (data == NULL || len == 0 || len > BSP_USB_WINUSB_RESPONSE_SIZE) {
        return BSP_STATUS_ERROR;
    }

    const unsigned long state = bsp_sys_enter_critical();
    if (!configured || suspended) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_OFFLINE;
    }
    if (winusb_tx_state != USB_TX_IDLE) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }

    winusb_tx_length = len;
    winusb_tx_state = USB_TX_DATA;
    if (usbd_ep_start_write(USB_BUS_ID, WINUSB_IN_EP, data, len) != 0) {
        winusb_tx_reset_locked();
        bsp_sys_exit_critical(state);
        return BSP_STATUS_ERROR;
    }
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

bsp_status_t bsp_usb_winusb_set_callback(bsp_usb_winusb_callback_t callback) {
    if (callback == NULL) return BSP_STATUS_ERROR;

    const unsigned long state = bsp_sys_enter_critical();
    winusb_rx_callback = callback;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

void usbd_cdc_acm_set_line_coding(
    uint8_t busid,
    uint8_t intf,
    struct cdc_line_coding *new_line_coding
) {
    (void) busid;
    (void) intf;
    line_coding = *new_line_coding;
}

void usbd_cdc_acm_get_line_coding(
    uint8_t busid,
    uint8_t intf,
    struct cdc_line_coding *current_line_coding
) {
    (void) busid;
    (void) intf;
    *current_line_coding = line_coding;
}
