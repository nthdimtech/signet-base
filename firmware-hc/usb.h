#ifndef USB_H
#include "types.h"
#include "signetdev_common.h"

void usb_send_bytes(int ep, const u8 *data, int length);
int usb_tx_pending(int ep);

#define CMD_HID_TX_SIZE 64
#define CMD_HID_RX_SIZE 64
#define FIDO_HID_TX_SIZE 64
#define FIDO_HID_RX_SIZE 64

#define LANGID_US_ENGLISH 0x409

#endif
