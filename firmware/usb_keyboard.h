#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include "types.h"

void usb_tx_keyboard();

void usb_keyboard_type(u8 *chars, u8 n);

void usb_keyboard_typing_done();

#endif
