#include "bsp/usb.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ring.h"
#include "cdc.h"
#include "bsp/sys.h"
#include "usb_dwc2_param.h"
#include "usbd_cdc_acm.h"
#include "usb_msc_port.h"
#include "usbd_msc.h"
#include "usbd_core.h"

enum { USB_BUS_ID = 0, USB_PACKET_SIZE = 64 };
enum { USB_RX_FIFO_WORDS = 5 + 8 + 64 / 4 + 1 + 2 * 8 + 1, USB_TX_FIFO_WORDS = 64 / 4 };

enum { CDC_CAPACITY = USB_CDC_COUNT > 0 ? USB_CDC_COUNT : 1 };
static bsp_usb_config_t active;
static bool initialized;
static char serial_number[16];
static uint8_t last_in, cdc_out_base, cdc_int_base, win_out, win_in, msc_out, msc_in;

#define CDC_OUT_EP(i) (cdc_out_base + (i))
#define CDC_IN_EP(i) (0x81U + 2U * (i))
#define CDC_INT_EP(i) (cdc_int_base + 2U * (i))

typedef enum {
    USB_TX_IDLE,
    USB_TX_ACTIVE,
    USB_TX_ZLP,
} usb_tx_state_e;

static void serial_append_hex(char *dst, uint32_t value, uint8_t digits) {
    for (uint8_t index = 0; index < digits; index++) {
        const uint8_t digit = (uint8_t) (value >> 28U);
        dst[index] = digit < 10U
            ? (char) ('0' + (char) digit)
            : (char) ('A' + (char) digit - 10);
        value <<= 4U;
    }
}

static void make_serial_number(void) {
    serial_append_hex(serial_number, HAL_GetUIDw0() + HAL_GetUIDw2(), 8);
    serial_append_hex(serial_number + 8, HAL_GetUIDw1(), 4);
    if (active.cdc_count == 1 && active.winusb && !active.disk) {
        serial_number[12] = 'C';
        serial_number[13] = '\0';
    } else {
        serial_number[12] = (char) ('0' + active.cdc_count);
        serial_number[13] = active.winusb ? 'W' : 'S';
        serial_number[14] = active.disk ? 'D' : 'N';
        serial_number[15] = '\0';
    }
}

static bool ep_supported(const struct dwc2_hw_params *hw, uint8_t ep) {
    const uint8_t idx = ep & 0x7fU;
    return idx <= hw->num_dev_ep && idx <= 7 &&
        ((hw->dev_ep_dirs >> (2U * idx)) & (ep & 0x80U ? 2U : 1U)) == 0;
}

static bool validate_fifo(void) {
    struct dwc2_hw_params hw;
    dwc2_get_hwparams(USB_OTG_HS_PERIPH_BASE, &hw);
    if (2U * active.cdc_count + active.winusb + (active.disk != NULL) > hw.num_dev_in_eps ||
        !hw.en_multiple_tx_fifo || !hw.enable_dynamic_fifo ||
        USB_RX_FIFO_WORDS + USB_TX_FIFO_WORDS * (last_in + 1U) > hw.total_fifo_size ||
        (USB_OTG_HS->GRXFSIZ & 0xFFFFU) < USB_RX_FIFO_WORDS) return false;
    for (uint8_t i = 0; i < active.cdc_count; i++) {
        if (!ep_supported(&hw, CDC_OUT_EP(i)) || !ep_supported(&hw, CDC_IN_EP(i)) ||
            !ep_supported(&hw, CDC_INT_EP(i))) return false;
    }
    if (active.winusb && (!ep_supported(&hw, win_out) || !ep_supported(&hw, win_in))) return false;
    if (active.disk && (!ep_supported(&hw, msc_out) || !ep_supported(&hw, msc_in))) return false;
    for (uint8_t ep = 0; ep <= last_in; ep++) {
        const uint32_t size = ep == 0 ? USB_OTG_HS->DIEPTXF0_HNPTXFSIZ >> 16U
                                      : USB_OTG_HS->DIEPTXF[ep - 1U] >> 16U;
        if (size < USB_TX_FIFO_WORDS) return false;
    }
    return true;
}

typedef struct {
    struct usbd_interface control;
    struct usbd_interface data;
    struct usbd_endpoint out;
    struct usbd_endpoint in;
    bsp_frame_ring_t tx_queue;
    USB_MEM_ALIGNX uint8_t tx_buffer[BSP_SERIAL_BUFFER_SIZE];
    USB_MEM_ALIGNX uint8_t rx_buffer[2][USB_PACKET_SIZE];
    struct cdc_line_coding line_coding;
    bsp_serial_callback_t callback;
    usb_tx_state_e tx_state;
    uint16_t tx_length;
    uint8_t rx_active;
    bool ready;
    uint32_t session;
} cdc_channel_t;

static cdc_channel_t cdc_channels[CDC_CAPACITY];
static bsp_usb_cdc_callback_t default_callback;

static void cdc_reset(cdc_channel_t *channel) {
    bsp_frame_ring_reset(&channel->tx_queue);
    channel->tx_state = USB_TX_IDLE;
    channel->tx_length = 0;
}

static int cdc_start_write(cdc_channel_t *channel) {
    return usbd_ep_start_write(
        USB_BUS_ID,
        channel->in.ep_addr,
        channel->tx_buffer,
        channel->tx_length
    );
}

static void cdc_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    const uint8_t index = (uint8_t) (ep - cdc_out_base);
    if (index >= active.cdc_count) return;
    cdc_channel_t *const channel = &cdc_channels[index];
    const uint8_t completed = channel->rx_active;
    channel->rx_active ^= 1U;

    const int read_status = usbd_ep_start_read(
        USB_BUS_ID,
        channel->out.ep_addr,
        channel->rx_buffer[channel->rx_active],
        USB_PACKET_SIZE
    );
    if (read_status != 0) {
        const unsigned long state = bsp_sys_enter_critical();
        channel->ready = false;
        bsp_sys_exit_critical(state);
    }
    if (nbytes <= USB_PACKET_SIZE && nbytes > 0 && channel->callback != NULL) {
        channel->callback(
            (bsp_serial_t) (E_USB_CDC_0 + index),
            channel->rx_buffer[completed],
            nbytes
        );
    }
}

static void cdc_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) nbytes;
    const uint8_t index = (uint8_t) (((ep & 0x7FU) - 1U) / 2U);
    if (index >= active.cdc_count) return;
    cdc_channel_t *const channel = &cdc_channels[index];

    const unsigned long state = bsp_sys_enter_critical();
    int status = 0;
    if (channel->tx_state == USB_TX_ACTIVE && channel->tx_length % USB_PACKET_SIZE == 0) {
        channel->tx_state = USB_TX_ZLP;
        status = usbd_ep_start_write(USB_BUS_ID, ep, NULL, 0);
    } else if (channel->tx_state != USB_TX_IDLE) {
        if (bsp_frame_ring_pop(&channel->tx_queue, channel->tx_buffer, &channel->tx_length)) {
            channel->tx_state = USB_TX_ACTIVE;
            status = cdc_start_write(channel);
        } else {
            channel->tx_state = USB_TX_IDLE;
        }
    }
    if (status != 0) cdc_reset(channel);
    bsp_sys_exit_critical(state);
}

#if USB_WINUSB_ENABLE
static bool win_ready;
static USB_MEM_ALIGNX uint8_t winusb_rx_buffer[2][BSP_USB_WINUSB_REQUEST_SIZE];
static uint16_t winusb_tx_length;
static uint8_t winusb_rx_active;
static usb_tx_state_e winusb_tx_state;
static bsp_usb_winusb_callback_t winusb_callback;
static struct usbd_interface winusb_interface;

static void winusb_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    const uint8_t completed = winusb_rx_active;
    winusb_rx_active ^= 1U;
    const int read_status = usbd_ep_start_read(
        USB_BUS_ID,
        ep,
        winusb_rx_buffer[winusb_rx_active],
        BSP_USB_WINUSB_REQUEST_SIZE
    );
    if (read_status != 0) {
        const unsigned long state = bsp_sys_enter_critical();
        win_ready = false;
        bsp_sys_exit_critical(state);
    }
    if (nbytes <= BSP_USB_WINUSB_REQUEST_SIZE && nbytes > 0 && winusb_callback != NULL) {
        winusb_callback(winusb_rx_buffer[completed], nbytes);
    }
}

static void winusb_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void) busid;
    (void) nbytes;
    const unsigned long state = bsp_sys_enter_critical();
    const bool send_zlp = winusb_tx_state == USB_TX_ACTIVE &&
        winusb_tx_length % USB_PACKET_SIZE == 0;
    if (send_zlp) {
        winusb_tx_state = usbd_ep_start_write(USB_BUS_ID, ep, NULL, 0) == 0
            ? USB_TX_ZLP : USB_TX_IDLE;
    } else {
        winusb_tx_state = USB_TX_IDLE;
    }
    bsp_sys_exit_critical(state);
}

static struct usbd_endpoint winusb_out = { .ep_addr = 0, .ep_cb = winusb_out_callback };
static struct usbd_endpoint winusb_in = { .ep_addr = 0, .ep_cb = winusb_in_callback };
#endif

static bool open_ep(uint8_t busid, uint8_t ep, uint8_t type, uint16_t mps) {
    const struct usb_endpoint_descriptor desc = {
        .bEndpointAddress = ep, .bmAttributes = type, .wMaxPacketSize = mps,
    };
    return usbd_ep_open(busid, &desc) == 0;
}

/* Transfers are stopped before a task may reuse an IN buffer or a new session is armed. */
static bool stop_ep(uint8_t busid, uint8_t ep) {
    const uintptr_t USBx_BASE = USB_OTG_HS_PERIPH_BASE;
    const unsigned index = ep & 0x7fU;
    if (ep & 0x80U) USBx_DEVICE->DIEPEMPMSK &= ~(1UL << index);
    if (usbd_ep_close(busid, ep) != 0) return false;
    if (ep & 0x80U) {
        if (USBx_INEP(index)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) return false;
        USBx_INEP(index)->DIEPINT = USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD;
    } else {
        if (USBx_OUTEP(index)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) return false;
        USBx_OUTEP(index)->DOEPINT = USB_OTG_DOEPINT_XFRC | USB_OTG_DOEPINT_EPDISD;
    }
    return true;
}

static bool reset_bulk(uint8_t busid, uint8_t out, uint8_t in, uint8_t event) {
    /* RESET clears the DWC2 transfer records. DEINIT stops the controller. */
    if (event == USBD_EVENT_RESET || event == USBD_EVENT_DEINIT) return true;
    const bool stopped_in = stop_ep(busid, in);
    if (!stop_ep(busid, out) || !stopped_in) return false;
    if (event != USBD_EVENT_CONFIGURED && event != USBD_EVENT_SET_INTERFACE) return true;
    return open_ep(busid, out, USB_ENDPOINT_TYPE_BULK, USB_PACKET_SIZE) &&
           open_ep(busid, in, USB_ENDPOINT_TYPE_BULK, USB_PACKET_SIZE);
}

static void cdc_event(uint8_t busid, cdc_channel_t *channel, uint8_t event) {
    channel->ready = false;
    channel->session++;
    if (!reset_bulk(busid, channel->out.ep_addr, channel->in.ep_addr, event)) return;
    cdc_reset(channel);
    if (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_SET_INTERFACE) {
        channel->rx_active = 0;
        channel->ready = usbd_ep_start_read(busid, channel->out.ep_addr,
                                            channel->rx_buffer[0], USB_PACKET_SIZE) == 0;
    }
}

#if USB_WINUSB_ENABLE
static void win_event(uint8_t busid, uint8_t event) {
    win_ready = false;
    if (!reset_bulk(busid, win_out, win_in, event)) return;
    winusb_tx_state = USB_TX_IDLE;
    if (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_SET_INTERFACE) {
        winusb_rx_active = 0;
        win_ready = usbd_ep_start_read(busid, win_out, winusb_rx_buffer[0], BSP_USB_WINUSB_REQUEST_SIZE) == 0;
    }
}
#endif

static void msc_event(uint8_t busid, uint8_t event) {
    usb_msc_cancel();
    if (reset_bulk(busid, msc_out, msc_in, event) &&
        (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_SET_INTERFACE))
        usb_msc_arm();
}

static bool msc_restart(void) {
    usb_msc_cancel();
    return reset_bulk(USB_BUS_ID, msc_out, msc_in, USBD_EVENT_SET_INTERFACE);
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *count, uint32_t *size) {
    (void) busid;
    (void) lun;
    *count = active.disk->block_count;
    *size = 512;
}

static void interface_event(uint8_t busid, uint8_t event, void *arg) {
    if (event != USBD_EVENT_SET_INTERFACE || !arg) return;
    const struct usb_interface_descriptor *desc = arg;
    if (desc->bAlternateSetting != 0) return;
    const uint8_t i = desc->bInterfaceNumber;
    if (i < 2U * active.cdc_count) {
        if (i & 1U) cdc_event(busid, &cdc_channels[i / 2U], event);
        else {
            /* There is no notification producer. Bulk data stays usable if reopening it fails. */
            (void) open_ep(busid, CDC_INT_EP(i / 2U), USB_ENDPOINT_TYPE_INTERRUPT, 8);
        }
        return;
    }
#if USB_WINUSB_ENABLE
    if (active.winusb && i == winusb_interface.intf_num) {
        win_event(busid, event);
        return;
    }
#endif
    if (active.disk) msc_event(busid, event);
}

static void event_handler(uint8_t busid, uint8_t event) {
    if (event != USBD_EVENT_CONFIGURED && event != USBD_EVENT_ERROR &&
        event != USBD_EVENT_RESET && event != USBD_EVENT_DISCONNECTED &&
        event != USBD_EVENT_DEINIT) return;
    for (uint8_t i = 0; i < active.cdc_count; ++i) cdc_event(busid, &cdc_channels[i], event);
#if USB_WINUSB_ENABLE
    if (active.winusb) win_event(busid, event);
#endif
    if (active.disk) msc_event(busid, event);
}

static const uint8_t *device_descriptor_callback(uint8_t speed);
static const uint8_t *config_descriptor_callback(uint8_t speed);
static const char *string_descriptor_callback(uint8_t speed, uint8_t index);

#if USB_WINUSB_ENABLE
static const uint8_t vendor_code = 0x17;
static const uint8_t win_function[] = {
    USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_MULTI_DESCRIPTOR_INIT(0)
};
static uint8_t ms_os_20_descriptor[WINUSB_DESCRIPTOR_SET_HEADER_SIZE + sizeof(win_function)];
USB_MEM_ALIGNX static uint8_t bos_data[5 + USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_LEN];
static struct usb_msosv2_descriptor ms_os_20 = {
    .compat_id = ms_os_20_descriptor,
    .vendor_code = vendor_code,
};
static const struct usb_bos_descriptor bos_descriptor = {
    .string = bos_data,
    .string_len = sizeof(bos_data),
};
#endif

static uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, 0x0483, 0x5741, 0x0101, 1)
};
static uint8_t config_descriptor[9 + CDC_CAPACITY * CDC_ACM_DESCRIPTOR_LEN +
                                 USB_WINUSB_ENABLE * 23 + MSC_DESCRIPTOR_LEN + 1];

static void make_descriptors(void) {
    const size_t len = 9 + active.cdc_count * CDC_ACM_DESCRIPTOR_LEN + 23 * active.winusb +
                       (active.disk ? MSC_DESCRIPTOR_LEN : 0);
    const uint8_t ifaces = 2 * active.cdc_count + active.winusb + (active.disk != NULL);
    const uint8_t header[] = { USB_CONFIG_DESCRIPTOR_INIT(len, ifaces, 1, USB_CONFIG_BUS_POWERED, 100) };
    uint8_t *p = config_descriptor;
    memcpy(p, header, sizeof(header));
    p += sizeof(header);
    for (uint8_t i = 0; i < active.cdc_count; i++) {
        const uint8_t cdc[] = {
            CDC_ACM_DESCRIPTOR_INIT(2 * i, CDC_INT_EP(i), CDC_OUT_EP(i), CDC_IN_EP(i), USB_PACKET_SIZE, 5)
        };
        memcpy(p, cdc, sizeof(cdc));
        p += sizeof(cdc);
    }
#if USB_WINUSB_ENABLE
    if (active.winusb) {
        const uint8_t win[] = {
            USB_INTERFACE_DESCRIPTOR_INIT(2 * active.cdc_count, 0, 2, 0xFF, 0, 0, 4),
            USB_ENDPOINT_DESCRIPTOR_INIT(win_out, USB_ENDPOINT_TYPE_BULK, USB_PACKET_SIZE, 0),
            USB_ENDPOINT_DESCRIPTOR_INIT(win_in, USB_ENDPOINT_TYPE_BULK, USB_PACKET_SIZE, 0)
        };
        memcpy(p, win, sizeof(win));
        p += sizeof(win);
        /* A standalone WinUSB device advertises the compatible ID at device scope. */
        const size_t skip = ifaces == 1 ? WINUSB_FUNCTION_SUBSET_HEADER_SIZE : 0;
        const uint16_t os_len = sizeof(ms_os_20_descriptor) - skip;
        const uint8_t os_header[] = { USB_MSOSV2_COMP_ID_SET_HEADER_DESCRIPTOR_INIT(os_len) };
        uint8_t *os = ms_os_20_descriptor;
        memcpy(os, os_header, sizeof(os_header));
        os += sizeof(os_header);
        memcpy(os, win_function + skip, sizeof(win_function) - skip);
        if (!skip) os[4] = 2 * active.cdc_count; /* bFirstInterface */
        ms_os_20.compat_id_len = os_len;
        const uint8_t bos[] = {
            USB_BOS_HEADER_DESCRIPTOR_INIT(sizeof(bos_data), 1),
            USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_INIT(vendor_code, os_len)
        };
        memcpy(bos_data, bos, sizeof(bos));
    }
#endif
    if (active.disk) {
        const uint8_t msc[] = { MSC_DESCRIPTOR_INIT(2 * active.cdc_count + active.winusb,
                                                   msc_out, msc_in, USB_PACKET_SIZE, 0) };
        memcpy(p, msc, sizeof(msc));
        p += sizeof(msc);
    }
    *p = 0;
    device_descriptor[2] = active.winusb ? (USB_2_1 & 0xffU) : 0;
    device_descriptor[4] = active.cdc_count ? 0xEF : 0;
    device_descriptor[5] = active.cdc_count ? 0x02 : 0;
    device_descriptor[6] = active.cdc_count ? 0x01 : 0;
    /* Windows caches OS descriptors by VID/PID/revision across device instances. */
    const bool legacy = active.cdc_count == 1 && active.winusb && !active.disk;
    device_descriptor[12] = legacy ? 1 : (active.cdc_count << 4) | (active.winusb << 1) | (active.disk != NULL);
    device_descriptor[13] = legacy ? 1 : 2;
}

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
        case 0: return "\x09\x04";
        case 1: return "trobot";
        case 2: return active.disk ? "dm-mc02 log disk" : "dm-mc02";
        case 3: return serial_number;
#if USB_WINUSB_ENABLE
        case 4: return "trobot winusb";
#endif
        case 5: return "trobot cdc";
        default: return NULL;
    }
}

static struct usb_descriptor descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

bsp_status_t bsp_usb_init(const bsp_usb_config_t *config) {
    if (config == NULL || config->cdc_count > CDC_CAPACITY || (config->winusb && !USB_WINUSB_ENABLE) ||
        (config->disk && (!config->disk->read || !config->disk->block_count))) return BSP_STATUS_ERROR;
    if (initialized) {
        return config->cdc_count == active.cdc_count && config->winusb == active.winusb && config->disk == active.disk
            ? BSP_STATUS_OK : BSP_STATUS_BUSY;
    }
    active = *config;
    if (!active.cdc_count && !active.winusb && !active.disk) {
        initialized = true;
        return BSP_STATUS_OK;
    }

    /* Keep the established Windows pipes for the original composite device. */
    const bool legacy = active.cdc_count == 1 && active.winusb && !active.disk;
    cdc_out_base = legacy ? 2 : 1;
    cdc_int_base = legacy ? 0x83 : 0x82;
    win_out = legacy ? 5 : 1 + active.cdc_count;
    win_in = legacy ? 0x84 : 0x81 + 2 * active.cdc_count;
    msc_out = 1 + active.cdc_count + active.winusb;
    msc_in = 0x81 + 2 * active.cdc_count + active.winusb;
    last_in = legacy ? 4 : 2 * active.cdc_count + active.winusb + (active.disk != NULL);

    /* Inspect reset FIFO depths before CherryUSB programs them or enables the USB IRQ. */
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_FORCE_RESET();
    __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
    if (!validate_fifo()) {
        __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
        return BSP_STATUS_ERROR;
    }
    make_serial_number();
    make_descriptors();
#if USB_WINUSB_ENABLE
    descriptor.msosv2_descriptor = active.winusb ? &ms_os_20 : NULL;
    descriptor.bos_descriptor = active.winusb ? &bos_descriptor : NULL;
#endif

    usbd_desc_register(USB_BUS_ID, &descriptor);
    for (uint8_t index = 0; index < active.cdc_count; index++) {
        cdc_channel_t *const channel = &cdc_channels[index];
        channel->line_coding = (struct cdc_line_coding) { 115200, 0, 0, 8 };
        channel->out = (struct usbd_endpoint) {
            .ep_addr = CDC_OUT_EP(index),
            .ep_cb = cdc_out_callback,
        };
        channel->in = (struct usbd_endpoint) {
            .ep_addr = CDC_IN_EP(index),
            .ep_cb = cdc_in_callback,
        };
        usbd_add_interface(USB_BUS_ID, usbd_cdc_acm_init_intf(USB_BUS_ID, &channel->control));
        usbd_add_interface(USB_BUS_ID, usbd_cdc_acm_init_intf(USB_BUS_ID, &channel->data));
        channel->control.notify_handler = interface_event;
        channel->data.notify_handler = interface_event;
        usbd_add_endpoint(USB_BUS_ID, &channel->out);
        usbd_add_endpoint(USB_BUS_ID, &channel->in);
    }
#if USB_WINUSB_ENABLE
    if (active.winusb) {
        winusb_out.ep_addr = win_out;
        winusb_in.ep_addr = win_in;
        winusb_interface.notify_handler = interface_event;
        usbd_add_interface(USB_BUS_ID, &winusb_interface);
        usbd_add_endpoint(USB_BUS_ID, &winusb_out);
        usbd_add_endpoint(USB_BUS_ID, &winusb_in);
    }
#endif
    if (active.disk) usb_msc_attach(active.disk, msc_out, msc_in, interface_event, msc_restart);
    if (usbd_initialize(USB_BUS_ID, USB_OTG_HS_PERIPH_BASE, event_handler) != 0) {
        (void) usbd_deinitialize(USB_BUS_ID);
        return BSP_STATUS_ERROR;
    }
    initialized = true;
    return BSP_STATUS_OK;
}

bool bsp_usb_is_configured(void) {
    const unsigned long state = bsp_sys_enter_critical();
    const bool result = initialized && usb_device_is_configured(USB_BUS_ID);
    bsp_sys_exit_critical(state);
    return result;
}

uint8_t bsp_usb_cdc_count(void) {
    return initialized ? active.cdc_count : 0;
}

uint32_t bsp_cdc_session(uint8_t index) {
    const unsigned long state = bsp_sys_enter_critical();
    const uint32_t session = cdc_channels[index].session;
    bsp_sys_exit_critical(state);
    return session;
}

bsp_status_t bsp_cdc_send(uint8_t index, const uint8_t *data, size_t len) {
    if (index >= CDC_CAPACITY || data == NULL || len == 0 || len > BSP_SERIAL_BUFFER_SIZE) {
        return BSP_STATUS_ERROR;
    }
    cdc_channel_t *const channel = &cdc_channels[index];
    bsp_status_t status = BSP_STATUS_OK;
    const unsigned long state = bsp_sys_enter_critical();
    if (!channel->ready || !usb_device_is_configured(USB_BUS_ID) || usb_device_is_suspend(USB_BUS_ID)) {
        status = BSP_STATUS_OFFLINE;
    } else if (channel->tx_state == USB_TX_IDLE) {
        memcpy(channel->tx_buffer, data, len);
        channel->tx_length = (uint16_t) len;
        if (cdc_start_write(channel) == 0) channel->tx_state = USB_TX_ACTIVE;
        else status = BSP_STATUS_ERROR;
    } else if (!bsp_frame_ring_push(&channel->tx_queue, data, (uint16_t) len)) {
        status = BSP_STATUS_BUSY;
    }
    bsp_sys_exit_critical(state);
    return status;
}

bsp_status_t bsp_cdc_set_callback(uint8_t index, bsp_serial_callback_t callback) {
    if (index >= CDC_CAPACITY || callback == NULL) return BSP_STATUS_ERROR;
    const unsigned long state = bsp_sys_enter_critical();
    if (cdc_channels[index].callback != NULL) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }
    cdc_channels[index].callback = callback;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

static void default_rx(bsp_serial_t port, const uint8_t *data, size_t len) {
    (void) port;
    default_callback(data, len);
}

bsp_status_t bsp_usb_cdc_send(const uint8_t *data, uint16_t len) {
    return bsp_cdc_send(0, data, len);
}

bsp_status_t bsp_usb_cdc_printf(const char *fmt, ...) {
    if (fmt == NULL) return BSP_STATUS_ERROR;
    va_list args;
    va_start(args, fmt);
    uint8_t buffer[BSP_USB_CDC_BUFFER_SIZE];
    const int len = vsnprintf((char *) buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len <= 0) return BSP_STATUS_ERROR;
    return bsp_usb_cdc_send(buffer, len < (int) sizeof(buffer) ? (uint16_t) len : sizeof(buffer) - 1U);
}

bsp_status_t bsp_usb_cdc_set_callback(bsp_usb_cdc_callback_t callback) {
    if (callback == NULL) return BSP_STATUS_ERROR;
    const unsigned long state = bsp_sys_enter_critical();
    if (cdc_channels[0].callback != NULL && cdc_channels[0].callback != default_rx) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }
    default_callback = callback;
    cdc_channels[0].callback = default_rx;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

#if USB_WINUSB_ENABLE
bsp_status_t bsp_usb_winusb_send(const uint8_t *data, uint16_t len) {
    if (data == NULL || len == 0 || len > BSP_USB_WINUSB_RESPONSE_SIZE ||
        ((uintptr_t) data & 3U) != 0U) return BSP_STATUS_ERROR;
    const unsigned long state = bsp_sys_enter_critical();
    if (!win_ready || !usb_device_is_configured(USB_BUS_ID) || usb_device_is_suspend(USB_BUS_ID)) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_OFFLINE;
    }
    if (winusb_tx_state != USB_TX_IDLE) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }
    if (usbd_ep_start_write(USB_BUS_ID, win_in, data, len) != 0) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_ERROR;
    }
    winusb_tx_length = len;
    winusb_tx_state = USB_TX_ACTIVE;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

bool bsp_usb_winusb_tx_busy(void) {
    const unsigned long state = bsp_sys_enter_critical();
    const bool result = winusb_tx_state != USB_TX_IDLE;
    bsp_sys_exit_critical(state);
    return result;
}

bsp_status_t bsp_usb_winusb_set_callback(bsp_usb_winusb_callback_t callback) {
    if (callback == NULL) return BSP_STATUS_ERROR;
    const unsigned long state = bsp_sys_enter_critical();
    winusb_callback = callback;
    bsp_sys_exit_critical(state);
    return BSP_STATUS_OK;
}

#else
bsp_status_t bsp_usb_winusb_send(const uint8_t *data, uint16_t len) {
    (void) data;
    (void) len;
    return BSP_STATUS_OFFLINE;
}

bool bsp_usb_winusb_tx_busy(void) { return false; }

bsp_status_t bsp_usb_winusb_set_callback(bsp_usb_winusb_callback_t callback) {
    (void) callback;
    return BSP_STATUS_OFFLINE;
}

#endif

const char *bsp_usb_serial_number(void) {
    return serial_number;
}

void dwc2_get_user_fifo_config(uint32_t reg_base, struct usb_dwc2_user_fifo_config *config) {
    (void) reg_base;
    *config = (struct usb_dwc2_user_fifo_config) { .device_rx_fifo_size = USB_RX_FIFO_WORDS };
    for (uint8_t ep = 0; ep <= last_in; ep++) config->device_tx_fifo_size[ep] = USB_TX_FIFO_WORDS;
}

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *coding) {
    (void) busid;
    cdc_channels[intf / 2U].line_coding = *coding;
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *coding) {
    (void) busid;
    *coding = cdc_channels[intf / 2U].line_coding;
}
