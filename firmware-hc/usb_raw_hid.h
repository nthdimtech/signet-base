#ifndef USB_RAW_HID_H
#define USB_RAW_HID_H

#include "usb.h"

int usb_raw_hid_rx(volatile u8 *data, int count);
void usb_raw_hid_tx();

#endif
