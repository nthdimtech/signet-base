#ifndef USB_STORAGE_H
#define USB_STORAGE_H

#include "usb.h"

int usb_storage_rx(volatile usbw_t *data, int count);
void usb_storage_tx();

#endif
