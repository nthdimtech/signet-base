#include <memory.h>

#include "usb_raw_hid.h"
#include "usb.h"
#include "commands.h"
#include "print.h"
#include "signetdev_common.h"
#include "usbd_multi.h"

static const u8 *raw_hid_tx_data = NULL;
static int raw_hid_tx_seq = 0;
static int raw_hid_tx_count = 0;

static u8 raw_hid_tx_cmd_packet[HID_CMD_EPIN_SIZE] __attribute__((aligned(16)));
static u8 raw_hid_tx_event_packet[HID_CMD_EPIN_SIZE] __attribute__((aligned(16)));

static u8 event_mask = 0;
static const u8 *event_data[8];
static int event_data_len[8];

static int maybe_send_raw_hid_event()
{
	if (!event_mask)
		return 0;
	if (usb_tx_pending(RAW_HID_TX_ENDPOINT))
		return 0;

	int i;
	for (i = 0; i < 8; i++) {
		if ((event_mask >> i) & 1) {
			event_mask &= ~(1<<i);
			raw_hid_tx_event_packet[0] = 0xff;
			raw_hid_tx_event_packet[RAW_HID_HEADER_SIZE] = i;
			if (event_data[i]) {
				raw_hid_tx_event_packet[RAW_HID_HEADER_SIZE + 1] = event_data_len[i];
				memcpy(raw_hid_tx_event_packet + RAW_HID_HEADER_SIZE + 2, event_data[i], event_data_len[i]);
			} else {
				raw_hid_tx_event_packet[RAW_HID_HEADER_SIZE + 1] = 0;
			}
			usb_send_bytes(HID_CMD_EPIN_ADDR, raw_hid_tx_event_packet, RAW_HID_PACKET_SIZE);
			break;
		}
	}
	return 1;
}

void cmd_packet_sent();

void maybe_send_raw_hid_packet()
{
	if (maybe_send_raw_hid_event())
		return;
	if (raw_hid_tx_seq != raw_hid_tx_count) {
		int last = 0;
		if ((raw_hid_tx_seq + 1) == raw_hid_tx_count) {
			last = 1;
		}
		raw_hid_tx_cmd_packet[0] = (last << 7) | raw_hid_tx_seq;
		memcpy(raw_hid_tx_cmd_packet + RAW_HID_HEADER_SIZE, raw_hid_tx_data + raw_hid_tx_seq * RAW_HID_PAYLOAD_SIZE, RAW_HID_PAYLOAD_SIZE);
		usb_send_bytes(HID_CMD_EPIN_ADDR, raw_hid_tx_cmd_packet, HID_CMD_EPIN_SIZE);
		raw_hid_tx_seq++;
	} else {
		raw_hid_tx_seq = 0;
		raw_hid_tx_count = 0;
		if (raw_hid_tx_data) {
			raw_hid_tx_data = NULL;
			cmd_packet_sent();
		}
	}
}

void cmd_packet_send(const u8 *data, u16 len)
{
	raw_hid_tx_count = (len + RAW_HID_PAYLOAD_SIZE - 1)/RAW_HID_PAYLOAD_SIZE;
	raw_hid_tx_seq = 0;
	raw_hid_tx_data = data;
	maybe_send_raw_hid_packet();
}

void cmd_event_send(int event_num, const u8 *data, int data_len)
{
	event_mask |= 1<<event_num;
	event_data[event_num]  = data;
	event_data_len[event_num] = data_len;
	maybe_send_raw_hid_event();
}

int usb_raw_hid_rx(volatile u8 *data, int count)
{
	u8 seq = data[0] & 0x7f;
	int last = (data[0] >> 7) & 0x1;
	int index = ((int)seq * RAW_HID_PAYLOAD_SIZE);
	if ((index + RAW_HID_PAYLOAD_SIZE) > CMD_PACKET_BUF_SIZE) {
		dprint_s("USB RAW HID: Data exceeds command buffer size\r\n");
		return last;
	}
	for(int i = RAW_HID_HEADER_SIZE; i < RAW_HID_PACKET_SIZE; i++) {
		u8 d = data[i];
		cmd_packet_buf[index++] = d;
	}
	if (last) {
		cmd_packet_recv();
	}
	return 1;
}

void usb_raw_hid_tx()
{
	maybe_send_raw_hid_packet();
}
