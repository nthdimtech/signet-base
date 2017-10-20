#ifndef USB_MOD_H
#define USB_MOD_H
#include "types.h"
int usb_send_descriptors(const u8 ** d, int restart_byte, int n_descriptor, int max_length);
void usb_send_bytes(int ep, const u8 *data, int length);
void usb_set_device_configuration(int conf_id, int length);
void usb_send_words(int ep, const u16 *data, int length);
void usb_send_bytes(int ep, const u8 *data, int length);
int usb_tx_pending(int ep);
void usb_valid_rx(int ep);
void usb_stall_tx(int ep);
void usb_stall_rx(int ep);
void usb_reset_device();
#endif
