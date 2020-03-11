#ifndef USB_RAW_HID_H
#define USB_RAW_HID_H

#include "usb.h"

void usb_raw_hid_rx(volatile u8 *data, int count);
void usb_raw_hid_tx();
void usb_raw_hid_rx_resume();

#endif
