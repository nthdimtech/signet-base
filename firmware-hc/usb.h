#ifndef USB_H
#include "types.h"
#include "signetdev_common.h"

void usb_send_bytes(int ep, const u8 *data, int length);
int usb_tx_pending(int ep);

#include "usbd_hid.h"

#define FIDO_HID_TX_SIZE (HID_FIDO_EPIN_SIZE)
#define FIDO_HID_RX_SIZE (HID_FIDO_EPOUT_SIZE)

#define LANGID_US_ENGLISH 0x409

#endif
