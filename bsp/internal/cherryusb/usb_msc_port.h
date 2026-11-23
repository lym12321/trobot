#pragma once

#include "bsp/usb.h"
#include "usbd_core.h"

void usb_msc_attach(const bsp_usb_disk_t *disk, uint8_t out, uint8_t in,
                    usbd_notify_handler notify, bool (*restart)(void));
void usb_msc_cancel(void);
bool usb_msc_arm(void);
