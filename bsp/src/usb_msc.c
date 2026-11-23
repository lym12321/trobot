#include "usb_msc_port.h"
#include "bsp/sys.h"

/* CherryUSB 1.6.1 keeps its BOT state private. Share it with the board adapter
 * so a reset during a deferred media read cannot submit stale data or status. */
#include "class/msc/usbd_msc.c"

static const bsp_usb_disk_t *media;
static struct usbd_interface interface;
static bool (*restart_endpoints)(void);
static uint32_t generation;
static bool armed, failed_in;
/* Only the polling task writes this sector. ISR commands use the class buffer. */
static USB_MEM_ALIGNX uint8_t sector[512];

void usb_msc_cancel(void) {
    generation++;
    armed = false;
    failed_in = false;
    g_usbd_msc[0].event = 0;
    usbd_msc_reset(0);
}

bool usb_msc_arm(void) {
    g_usbd_msc[0].stage = MSC_READ_CBW;
    /* Reuse the class response buffer for a full bulk packet until the CBW is validated. */
    armed = usbd_ep_start_read(0, mass_ep_data[0][MSD_OUT_EP_IDX].ep_addr,
                               g_usbd_msc[0].block_buffer, 64) == 0;
    return armed;
}

static void fail_command(uint32_t sense) {
    struct usbd_msc_priv *m = &g_usbd_msc[0];
    SCSI_SetSenseData(0, sense);
    if (m->csw.dDataResidue) {
        if (m->cbw.bmFlags & 0x80U) {
            /* End the IN data phase with a short packet before returning the CSW. */
            failed_in = true;
            if (usbd_ep_start_write(0, mass_ep_data[0][MSD_IN_EP_IDX].ep_addr, NULL, 0))
                usb_msc_cancel();
            return;
        }
        if (usbd_ep_set_stall(0, mass_ep_data[0][MSD_OUT_EP_IDX].ep_addr)) {
            usb_msc_cancel();
            return;
        }
    }
    usbd_msc_send_csw(0, CSW_STATUS_CMD_FAILED);
}

static void bulk_out(uint8_t busid, uint8_t ep, uint32_t len) {
    (void) ep;
    struct usbd_msc_priv *m = &g_usbd_msc[0];
    if (!armed || m->stage != MSC_READ_CBW) return;
    armed = false;
    if (len == sizeof(m->cbw)) memcpy(&m->cbw, m->block_buffer, sizeof(m->cbw));
    if (len != sizeof(m->cbw) || m->cbw.dSignature != MSC_CBW_Signature ||
        !m->cbw.bCBLength || m->cbw.bCBLength > sizeof(m->cbw.CB)) {
        (void) usbd_ep_set_stall(busid, mass_ep_data[0][MSD_IN_EP_IDX].ep_addr);
        (void) usbd_ep_set_stall(busid, mass_ep_data[0][MSD_OUT_EP_IDX].ep_addr);
        return;
    }
    m->csw.dTag = m->cbw.dTag;
    m->csw.dDataResidue = m->cbw.dDataLength;
    const uint8_t *cmd = m->cbw.CB;
    const uint8_t group = cmd[0] >> 5;
    const unsigned cmd_len = group == 0 ? 6 : group == 5 ? 12 : 10;
    if (m->cbw.bLUN || (m->cbw.bmFlags & 0x7fU) || m->cbw.bCBLength < cmd_len) {
        fail_command(SCSI_KCQIR_INVALIDFIELDINCBA);
        return;
    }
    if (cmd[0] == SCSI_CMD_WRITE10 || cmd[0] == SCSI_CMD_WRITE12) {
        fail_command(0x072700); /* DATA PROTECT / write protected */
        return;
    }
    if (m->cbw.dDataLength && !m->cbw.bmFlags) {
        fail_command(SCSI_KCQIR_INVALIDFIELDINCBA);
        return;
    }
    if (cmd[0] == SCSI_CMD_READ10 || cmd[0] == SCSI_CMD_READ12) {
        const uint32_t lba = GET_BE32(cmd + 2);
        const uint32_t count = cmd[0] == SCSI_CMD_READ10 ? GET_BE16(cmd + 7) : GET_BE32(cmd + 6);
        if (lba > media->block_count || count > media->block_count - lba) {
            fail_command(SCSI_KCQIR_LBAOUTOFRANGE);
            return;
        }
        if (m->cbw.dDataLength % sizeof(sector) || count != m->cbw.dDataLength / sizeof(sector)) {
            fail_command(SCSI_KCQIR_INVALIDFIELDINCBA);
            return;
        }
        if (count == 0) {
            usbd_msc_send_csw(0, CSW_STATUS_CMD_PASSED);
            return;
        }
    }
    if (cmd[0] == SCSI_CMD_MODESENSE10) {
        /* No block descriptors or cached writes. Bit 7 of device-specific parameters is WP. */
        static USB_MEM_ALIGNX uint8_t mode[8] = {0, 6, 0, 0x80};
        usbd_msc_send_info(0, mode, MIN(GET_BE16(cmd + 7), sizeof(mode)));
        return;
    }
    if (!SCSI_CBWDecode(busid, sizeof(m->cbw))) {
        const uint32_t sense = ((uint32_t) m->sKey << 16) | ((uint32_t) m->ASC << 8) | m->ASQ;
        fail_command(sense);
    }
}

static void bulk_in(uint8_t busid, uint8_t ep, uint32_t len) {
    if (failed_in) {
        failed_in = false;
        usbd_msc_send_csw(busid, CSW_STATUS_CMD_FAILED);
    } else if (g_usbd_msc[0].stage == MSC_WAIT_CSW) {
        usb_msc_arm();
    } else {
        mass_storage_bulk_in(busid, ep, len);
    }
}

static int request(uint8_t busid, struct usb_setup_packet *setup, uint8_t **data, uint32_t *len) {
    if (setup->wIndex != interface.intf_num || setup->wValue) return -1;
    if (setup->bRequest == MSC_REQUEST_RESET && setup->bmRequestType == 0x21 && !setup->wLength) {
        return restart_endpoints() && usb_msc_arm() ? 0 : -1;
    }
    if (setup->bRequest == MSC_REQUEST_GET_MAX_LUN && setup->bmRequestType == 0xA1 && setup->wLength == 1)
        return msc_storage_class_interface_request_handler(busid, setup, data, len);
    return -1;
}

void usb_msc_attach(const bsp_usb_disk_t *disk, uint8_t out, uint8_t in,
                    usbd_notify_handler notify, bool (*restart)(void)) {
    media = disk;
    restart_endpoints = restart;
    usbd_msc_init_intf(0, &interface, out, in);
    usbd_msc_set_readonly(0, true);
    interface.class_interface_handler = request;
    interface.notify_handler = notify;
    usbd_add_interface(0, &interface);
    mass_ep_data[0][MSD_OUT_EP_IDX].ep_cb = bulk_out;
    mass_ep_data[0][MSD_IN_EP_IDX].ep_cb = bulk_in;
    usbd_add_endpoint(0, &mass_ep_data[0][MSD_OUT_EP_IDX]);
    usbd_add_endpoint(0, &mass_ep_data[0][MSD_IN_EP_IDX]);
}

void bsp_usb_poll(void) {
    struct usbd_msc_priv *m = &g_usbd_msc[0];
    unsigned long state = bsp_sys_enter_critical();
    if (m->event != MSC_DATA_IN || !usb_device_is_configured(0) || usb_device_is_suspend(0)) {
        bsp_sys_exit_critical(state);
        return;
    }
    m->event = 0;
    const uint32_t session = generation;
    const uint32_t lba = m->start_sector;
    bsp_sys_exit_critical(state);

    const bsp_status_t status = media->read(media->ctx, lba, sector, 1);
    state = bsp_sys_enter_critical();
    if (generation == session && usb_device_is_configured(0) && m->stage == MSC_DATA_IN) {
        if (status != BSP_STATUS_OK) {
            fail_command(0x031100); /* MEDIUM ERROR / unrecovered read */
        } else if (usbd_ep_start_write(0, mass_ep_data[0][MSD_IN_EP_IDX].ep_addr, sector, sizeof(sector))) {
            usb_msc_cancel();
        } else {
            m->start_sector++;
            m->csw.dDataResidue -= sizeof(sector);
            if (--m->nsectors == 0) m->stage = MSC_SEND_CSW;
        }
    }
    bsp_sys_exit_critical(state);
}
