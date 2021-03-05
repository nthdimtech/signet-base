#include <memory.h>

#include "usb_raw_hid.h"
#include "usb.h"
#include "commands.h"
#include "print.h"
#include "signetdev_common.h"
#include "usbd_multi.h"
#include "config.h"

#include "usbd_hid.h"


static struct {
	u8 tx_cmd_packet[HID_CMD_EPIN_SIZE] __attribute__((aligned(16)));
	u8 tx_event_packet[HID_CMD_EPIN_SIZE] __attribute__((aligned(16)));
	const u8 *tx_data;
	int tx_seq;
	int tx_count;


	u8 event_mask;
	const u8 *event_data[8];
	int event_data_len[8];
} s_raw_hid __attribute__((aligned(16)));

static int maybe_send_raw_hid_event()
{
	if (!s_raw_hid.event_mask)
		return 0;
	if (usb_tx_pending(RAW_HID_TX_ENDPOINT))
		return 0;

	int i;
	for (i = 0; i < 8; i++) {
		if ((s_raw_hid.event_mask >> i) & 1) {
			s_raw_hid.event_mask &= ~(1<<i);
			s_raw_hid.tx_event_packet[0] = 0xff;
			s_raw_hid.tx_event_packet[RAW_HID_HEADER_SIZE] = i;
			if (s_raw_hid.event_data[i]) {
				s_raw_hid.tx_event_packet[RAW_HID_HEADER_SIZE + 1] = s_raw_hid.event_data_len[i];
				memcpy(s_raw_hid.tx_event_packet + RAW_HID_HEADER_SIZE + 2, s_raw_hid.event_data[i], s_raw_hid.event_data_len[i]);
			} else {
				s_raw_hid.tx_event_packet[RAW_HID_HEADER_SIZE + 1] = 0;
			}
			usb_send_bytes(HID_CMD_EPIN_ADDR, s_raw_hid.tx_event_packet, RAW_HID_PACKET_SIZE);
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
	if (s_raw_hid.tx_seq != s_raw_hid.tx_count) {
		int last = 0;
		if ((s_raw_hid.tx_seq + 1) == s_raw_hid.tx_count) {
			last = 1;
		}
		s_raw_hid.tx_cmd_packet[0] = (last << 7) | s_raw_hid.tx_seq;
		memcpy(s_raw_hid.tx_cmd_packet + RAW_HID_HEADER_SIZE, s_raw_hid.tx_data + s_raw_hid.tx_seq * RAW_HID_PAYLOAD_SIZE, RAW_HID_PAYLOAD_SIZE);
		usb_send_bytes(HID_CMD_EPIN_ADDR, s_raw_hid.tx_cmd_packet, HID_CMD_EPIN_SIZE);
		s_raw_hid.tx_seq++;
	} else {
		s_raw_hid.tx_seq = 0;
		s_raw_hid.tx_count = 0;
		if (s_raw_hid.tx_data) {
			s_raw_hid.tx_data = NULL;
			cmd_packet_sent();
		}
	}
}

void cmd_packet_send(const u8 *data, u16 len)
{
	s_raw_hid.tx_count = (len + RAW_HID_PAYLOAD_SIZE - 1)/RAW_HID_PAYLOAD_SIZE;
	s_raw_hid.tx_seq = 0;
	s_raw_hid.tx_data = data;
	maybe_send_raw_hid_packet();
}

void cmd_event_send(int event_num, const u8 *data, int data_len)
{
	s_raw_hid.event_mask |= 1<<event_num;
	s_raw_hid.event_data[event_num]  = data;
	s_raw_hid.event_data_len[event_num] = data_len;
	maybe_send_raw_hid_event();
}

void usb_raw_hid_rx(volatile u8 *data, int count)
{
	u8 seq = data[0] & 0x7f;
	int last = (data[0] >> 7) & 0x1;
	int index = ((int)seq * RAW_HID_PAYLOAD_SIZE);
	if ((index + RAW_HID_PAYLOAD_SIZE) > CMD_PACKET_BUF_SIZE) {
		USBD_HID_rx_resume(INTERFACE_CMD);
	}
	for(int i = RAW_HID_HEADER_SIZE; i < RAW_HID_PACKET_SIZE; i++) {
		u8 d = data[i];
		g_cmd_packet_buf[index++] = d;
	}
	if (last) {
		cmd_packet_recv();
	} else {
		USBD_HID_rx_resume(INTERFACE_CMD);
	}
}

void usb_raw_hid_tx()
{
	maybe_send_raw_hid_packet();
}
