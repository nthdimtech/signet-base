#include "usb_storage.h"
#include "print.h"
#include "mem.h"
#include "usb.h"
#include "usb_driver.h"
#include "flash.h"
#include "common.h"


#if USE_STORAGE

#define STATUS_PASS 0
#define STATUS_FAIL 1

#define SCSI_TEST_UNIT_READY            0x00
#define SCSI_REQUEST_SENSE              0x03
#define SCSI_FORMAT_UNIT                0x04
#define SCSI_INQUIRY                    0x12
#define SCSI_MODE_SELECT6               0x15
#define SCSI_MODE_SENSE6                0x1A
#define SCSI_START_STOP_UNIT            0x1B
#define SCSI_MEDIA_REMOVAL              0x1E
#define SCSI_READ_FORMAT_CAPACITIES     0x23
#define SCSI_READ_CAPACITY              0x25
#define SCSI_READ10                     0x28
#define SCSI_WRITE10                    0x2A
#define SCSI_VERIFY10                   0x2F
#define SCSI_MODE_SELECT10              0x55
#define SCSI_MODE_SENSE10               0x5A

#define MODE_SENSE_PC_CURRENT 0
#define MODE_SENSE_PC_CHANGABLE 1
#define MODE_SENSE_PC_DEFAULT 2
#define MODE_SENSE_PC_SAVED 3

#define MODE_PAGE_MEDIA_TYPE 0x0
#define MODE_PAGE_RW_ERR 0x1
#define MODE_PAGE_FLEXIBLE_DISK 0x5
#define MODE_PAGE_REMOVABLE_BLOCK 0x1B
#define MODE_PAGE_TIMER_AND_PROTECT 0x1C
#define MODE_PAGE_ALL 0x3F

static const usbw_t cbw_sig[8] = {
	(0x53 << 8) + 0x55,
	(0x43 << 8) + 0x42,
};

static const u8 inquery_resp[36] = {
	0x0,
	0x80,  //Removable
	0, //No ISO, ECMA, or ANSI
	1,

	0x1F,
	0x80,
	0x00,
	0x00,

	'n',
	't',
	'h',
	'd',

	'i',
	'm',
	't',
	'e',

	's',
	'i',
	'g',
	'n',

	'e',
	't',
	' ',
	' ',

	' ',
	' ',
	' ',
	' ',

	' ',
	' ',
	' ',
	' ',

	'1',
	'.',
	'0',
	' '
};

static const u8 read_capacity_resp[8] = {
	((NUM_MASS_STORAGE_BLOCKS-1) >> 24) & 0xff,
	((NUM_MASS_STORAGE_BLOCKS-1) >> 16) & 0xff,
	((NUM_MASS_STORAGE_BLOCKS-1) >> 8) & 0xff,
	((NUM_MASS_STORAGE_BLOCKS-1) >> 0) & 0xff,
	(FLASH_PAGE_SIZE >> 24) & 0xff,
	(FLASH_PAGE_SIZE >> 16) & 0xff,
	(FLASH_PAGE_SIZE >> 8) & 0xff,
	(FLASH_PAGE_SIZE >> 0) & 0xff
};

#define CMD_PHASE_READY 0
#define CMD_PHASE_DATA_TX 1
#define CMD_PHASE_DATA_RX 2
#define CMD_PHASE_CSW_SENT 3

static int cmd_phase = CMD_PHASE_READY;
static u8 cmd_opcode;
static u32 cmd_tag;
static u32 cmd_tx_req_len;
static u32 cmd_tx_len;
static u32 cmd_blk_count;
static u32 cmd_blk_lba;
static u32 cmd_blk_writing = 0;
static u8 tx_ingress[BLK_SIZE];
static u16 tx_index;

extern struct root_page _root_page;

#define FIRST_MASS_STORAGE_BLOCK (NUM_STORAGE_BLOCKS-NUM_MASS_STORAGE_BLOCKS)

//static u8 tx_blk[512]; //92 == 32 + 64
#define DATA_BLK(id) (((u8 *)&_root_page) + BLK_SIZE * (FIRST_MASS_STORAGE_BLOCK+id))

u32 read_bigend_u32(const u8 *src)
{
	return (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
}

void write_bigend_u32(u8 *dest, u32 src)
{
	dest[0] = (src >> 24) & 0xff;
	dest[1] = (src >> 16) & 0xff;
	dest[2] = (src >> 8) & 0xff;
	dest[3] = (src >> 0) & 0xff;
}

u16 read_bigend_u16(const u8 *src)
{
	return ((u16)src[0] << 8) | src[1];
}

void write_bigend_u16(u8 *dest, u16 src)
{
	dest[0] = (src >> 8) & 0xff;
	dest[1] = (src >> 0) & 0xff;
}

void write_u32(u8 *dest, u32 src)
{
	dest[0] = src & 0xff;
	dest[1] = (src >> 8) & 0xff;
	dest[2] = (src >> 16) & 0xff;
	dest[3] = (src >> 24) & 0xff;
}

static void send_csw(u8 stat)
{
	static u8 csw[13];
	write_u32(csw, 0x53425355);
	write_u32(csw + 4, cmd_tag);
	u32 residue = cmd_tx_req_len - cmd_tx_len;
	write_u32(csw + 8, residue);
	csw[12] = stat;
	usb_send_bytes(STORAGE_TX_ENDPOINT, csw, sizeof(csw));
	cmd_phase = CMD_PHASE_CSW_SENT;
}

static void write_data_out(const u8 *data, u32 count)
{
	usb_send_bytes(STORAGE_TX_ENDPOINT, data, count);
	cmd_tx_len += count;
	cmd_phase = CMD_PHASE_DATA_TX;
}

static int handle_cbw(u32 tag,
	u32 tx_len,
	int dir,
	u8 lun,
	u8 cb_len,
	u8 *cb)
{
	if (cmd_phase != CMD_PHASE_READY) {
		dprint_s("CBW sent while not ready\r\n");
		return 1;
	}
	int i;
	(void)i;
	cmd_opcode = cb[0];
	cmd_tag = tag;
	cmd_tx_req_len = tx_len;
	cmd_tx_len = 0;

	int matched = 1;

	switch (cb[0]) {
	case SCSI_TEST_UNIT_READY:
		send_csw(STATUS_PASS);
		break;
	case SCSI_REQUEST_SENSE: {
		static u8 request_sense_resp[18] = {
			0x70,
			0x00,
			0x02,

			0,
			0,
			0,
			0,

			0xA,

			0,
			0,
			0,
			0,

			0x30,
			0x1,
			0,
			0,
			0,
			0,
		};
		write_data_out(request_sense_resp, sizeof(request_sense_resp));
		} break;
	case SCSI_FORMAT_UNIT:
		matched = 0;
		break;
	case SCSI_INQUIRY:
		dprint_s("USB CBW INQUERY\r\n");
		write_data_out(inquery_resp, sizeof(inquery_resp));
		break;
	case SCSI_MODE_SELECT6:
		matched = 0;
		break;
	case SCSI_MODE_SENSE6: {
		dprint_s("USB CBW MODE SENSE\r\n");
		//This basically says we don't use mode sense and mode select
		//because we have no mode pages
		static u8 mode_sense_resp[4] = {
			3,
			0, //6 more bytes to go
			0,
			0
		};
		write_data_out(mode_sense_resp, sizeof(mode_sense_resp));
		} break;
	case SCSI_START_STOP_UNIT: {
			u8 temp = cb[4];
			u8 pwr_cond = temp >> 4;
			int start = temp & 1;
			int loej = temp & 2;
			switch (pwr_cond) {
			case 0:
				if (start) {
					dprint_s("USB CBW START STOP UNIT: START\r\n");
				} else if (!loej) {
					dprint_s("USB CBW START STOP UNIT: STOP\r\n");
				} else {
					dprint_s("USB CBW START STOP UNIT: STOP EJECT\r\n");
				}
				break;
			default:
				dprint_s("USB CBW START STOP UNIT: pwr cond = ");
				dprint_dec(pwr_cond);
				dprint_s("\r\n");
				break;
			}
			send_csw(STATUS_PASS);
		} break;
	case SCSI_MEDIA_REMOVAL: {
			//TODO: understand control byte 5
			int prevent = cb[4] & 0x3;
			switch (prevent) {
			case 0:
				dprint_s("USB CBW MEDIA REMOVAL ALLOW\r\n");
				send_csw(STATUS_PASS);
				break;
			case 1:
				dprint_s("USB CBW MEDIA REMOVAL PREVENT\r\n");
				send_csw(STATUS_PASS);
				break;
			default:
				dprint_s("USB CBW MEDIA REMOVAL ???\r\n");
				send_csw(STATUS_FAIL);
				break;
			}
		} break;
	case SCSI_READ_FORMAT_CAPACITIES:
		{
		static u8 resp[] = {
			0,0,0,0x8,

			0,0,0,32,

			2,0,(2048>>8),0
		};
		write_data_out(resp, sizeof(resp));
		} break;
	case SCSI_READ_CAPACITY:
		dprint_s("USB CBW READ CAPACITY\r\n");
		write_data_out(read_capacity_resp, sizeof(read_capacity_resp));
		break;
	case SCSI_READ10 : {
		u32 lba = read_bigend_u32(cb + 2);
		u16 tx_len = read_bigend_u16(cb + 7);
		dprint_s("USB CBW READ10");
		dprint_s(" tx LBA = "); dprint_hex(lba);
		dprint_s(", tx_len = "); dprint_dec(tx_len);
		dprint_s("\r\n");
		cmd_blk_count = tx_len;
		cmd_blk_lba = lba;
		if (tx_len) {
			write_data_out(DATA_BLK(cmd_blk_lba), BLK_SIZE);
		} else {
			send_csw(STATUS_PASS);
		}
		} break;
	case SCSI_WRITE10: {
		u32 lba = read_bigend_u32(cb + 2);
		u32 tx_len = read_bigend_u16(cb + 7);
		dprint_s("USB CBW WRITE1");
		dprint_s(" tx LBA = "); dprint_hex(lba);
		dprint_s(", tx_len = "); dprint_dec(tx_len);
		dprint_s("\r\n");
		cmd_blk_count = tx_len;
		cmd_blk_lba = lba;
		if (cmd_blk_count > 0) {
			cmd_blk_writing = 1;
			cmd_phase = CMD_PHASE_DATA_RX;
			tx_index = 0;
			return 1;
		} else {
			send_csw(STATUS_PASS);
		}
		} break;
	case  SCSI_VERIFY10:
		matched = 0;
		break;
	default:
		matched = 0;
	}
	if (!matched) {
		dprint_s("USB CBW cmd =");
		dprint_hex(cb[0]);
		dprint_s(", tag=");
		dprint_hex(tag);
		dprint_s(", txlen = "); dprint_dec(tx_len);
		dprint_s(", dir = "); dprint_dec(dir);
		dprint_s(", lun = "); dprint_dec(lun);
		dprint_s("\r\n");
		dprint_s("Unknown command:  ");
		for (i = 0; i < cb_len; i++) {
			dprint_hex(cb[i]);
			dprint_s(" ");
		}
		dprint_s("\r\n");
		send_csw(STATUS_FAIL);
	}
	return 0;
}

void flash_write_storage_complete()
{
	if (cmd_blk_writing) {
		if (cmd_blk_count == 0) {
			cmd_blk_writing = 0;
			send_csw(STATUS_PASS);
		} else {
			usb_valid_rx(STORAGE_RX_ENDPOINT);
		}
	}
}

int usb_storage_rx(volatile usbw_t *data, int count)
{
	if (count == 0x1F) {
		int i;
		for (i = 0; i < 2; i++) {
			if (data[i] != cbw_sig[i])
				break;
		}
		if (i == 2) {
			u32 tag = data[2] + (data[3] << 16);
			u32 tx_len = data[4] + (data[5] << 16);
			usbw_t d6 = data[6];
			usbw_t d7 = data[7];
			u8 cb[17];
			cb[0] = (d7 >> 8) & 0xff;
			int j = 8;
			for (i = 1; i < 17; i +=2) {
				usbw_t a = data[j];
				cb[i] = a & 0xff;
				cb[i + 1] = (a >> 8) & 0xff;
				j++;
			}
			int rx_valid = handle_cbw(tag,
					tx_len,
					(d6 >> 7) & 1,
					(d6 >> 8) & 0xf,
					d7 & 0x1f,
					cb);
			return rx_valid;
		}
	} else {
		if (cmd_blk_writing) {
			int i = 0;
			for (i = 0; i < (count/2); i++) {
				usbw_t w = data[i];
				tx_ingress[tx_index] = w & 0xff;
				tx_ingress[tx_index + 1] = (w >> 8) & 0xff;
				tx_index += 2;
			}
			cmd_tx_len += count;
			if (tx_index >= BLK_SIZE) {
				flash_write_page(DATA_BLK(cmd_blk_lba), tx_ingress, BLK_SIZE);
				cmd_blk_lba++;
				cmd_blk_count--;
				tx_index = 0;
				return 0;
			}
		}
		return 1;
	}
	dprint_s("Unknown RX packet\r\n");
	return 1;
}

void usb_storage_tx()
{
	switch (cmd_phase) {
	case CMD_PHASE_DATA_TX:	{
		switch (cmd_opcode) {
		case SCSI_READ10:
			cmd_blk_count--;
			cmd_blk_lba++;
			if (cmd_blk_count == 0) {
				send_csw(STATUS_PASS);
			} else {
				write_data_out(DATA_BLK(cmd_blk_lba), BLK_SIZE);
			}
			break;
		default:
			cmd_tx_req_len = 0;
			cmd_tx_len = 0;
			send_csw(STATUS_PASS);
			break;
		}
	} break;
	case CMD_PHASE_DATA_RX: {
	} break;
	case CMD_PHASE_CSW_SENT: {
		usb_valid_rx(STORAGE_RX_ENDPOINT);
		cmd_phase = CMD_PHASE_READY;
	} break;
	}
}
#endif
