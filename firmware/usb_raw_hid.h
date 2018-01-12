#ifndef USB_RAW_HID_H
#define USB_RAW_HID_H

#include "usb.h"
#include "regmap.h"

int usb_raw_hid_rx(volatile usbw_t *data, int count);
void usb_raw_hid_tx();

#endif
