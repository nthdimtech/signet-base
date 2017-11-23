#include "usb_serial.h"

#include "commands.h"
#include "signetdev/common/signetdev_common.h"
#include "types.h"
#include "usb.h"
#include "print.h"
#include "regmap.h"
void cmd_packet(u8 *data);
void cmd_connect();
void cmd_disconnect();

#ifndef USE_RAW_HID
static u8 packet_size = 0;
static u8 packet_size_bytes = 0;
static u8 packet_pos = 0;

void cmd_packet_send(const u8 *data)
{
	usb_send_bytes(CDC_TX_ENDPOINT, data, data[0]);
}
#endif

void usb_serial_tx()
{
}

int usb_serial_rx(volatile usbw_t *data, int count)
{
#ifndef USE_RAW_HID
	int i = 0;
	while (i < count) {
		if (packet_size_bytes < 2) {
			int d = (data[i>>1] >> (8*(i&1))) & 0xff;
			packet_size += d << (packet_size_bytes * 8);
			packet_pos = 0;
			packet_size_bytes++;
			if (packet_size_bytes == 2 && packet_size > CMD_PACKET_BUF_SIZE) {
				dprint_s("Packet size ");
				dprint_dec(packet_size);
				dprint_s(" too large\r\n");
				break;
			}
			i++;
			continue;
		}
		while (i < count && packet_pos < packet_size) {
			u8 d = (data[i>>1] >> (8*(i&1))) & 0xff;
			cmd_packet_buf[packet_pos++] = d;
			i++;
		}
		if (packet_pos == packet_size) {
			int rc = cmd_packet_recv();
			packet_size = 0;
			packet_size_bytes = 0;
			packet_pos = 0;
			if (i != count) {
				dprint_s("ERROR: Overlapped commands\r\n");
			}
			return rc;
		}
	}
#endif
	return 1;
}

struct serial_line_coding g_serial_line_coding = {
	115200, 0, 0, 8
};

void usb_serial_line_state(int rx_enable, int tx_enable)
{
	dprint_s("USB CDC/ACM: SET_CONTROL_LINE_STATE: rx =  ");
	dprint_dec(rx_enable);
	dprint_s(", tx = ");
	dprint_dec(tx_enable);
	dprint_s("\r\n");
#ifndef USE_RAW_HID
	if (rx_enable && tx_enable) {
		packet_size_bytes = 0;
		packet_size = 0;
		cmd_connect();
	}
	if (!rx_enable || !tx_enable) {
		cmd_disconnect();
	}
#endif
}
