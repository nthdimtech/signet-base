#include "regmap.h"
#include "usb_driver.h"
#include "config.h"
#include "usb.h"
#include "print.h"

#if defined(MCU_STM32F303XC)
#define USB_PMA_PTR(addr) ((volatile usbw_t *)(USB_PMA_ADDR + addr * 2))
#elif defined(MCU_STM32L443XC)
#define USB_PMA_PTR(addr) ((volatile usbw_t *)(USB_PMA_ADDR + addr))
#else
#error Unknown MCU
#endif

static int tx_pending[8] = {0,0,0,0,0,0,0,0};
static u8 *tx_next[8] = {0,0,0,0,0,0,0,0};
static u8 *tx_end[8] = {0,0,0,0,0,0,0,0};
static int tx_max[8] = {
	[CONTROL_ENDPOINT]=CTRL_TX_SIZE,
#if USE_STORAGE
	[STORAGE_TX_ENDPOINT]=STORAGE_TX_SIZE,
	[STORAGE_RX_ENDPOINT]=STORAGE_RX_SIZE,
#else
	[CDC_ACM_ENDPOINT]=CDC_ACM_PACKET_SIZE,
	[CDC_TX_ENDPOINT]=CDC_TX_SIZE,
#endif
	[KEYBOARD_ENDPOINT]=KEYBOARD_PACKET_SIZE,
	[RAW_HID_TX_ENDPOINT] = RAW_HID_TX_SIZE};

void usb_stall_tx(int id)
{
	int v = USB_EPR(id);
	USB_EPR(id) =
		USB_EPR_CTR_RX |
		USB_EPR_CTR_TX |
		(v & (USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK | USB_EPR_EA_MASK)) |
		((v & USB_EPR_STAT_TX_MASK) ^ USB_EPR_STAT_TX_STALL);
}

static void usb_valid_tx(int id)
{
	int v = USB_EPR(id);
	USB_EPR(id) =
		USB_EPR_CTR_RX |
		USB_EPR_CTR_TX |
		(v & (USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK | USB_EPR_EA_MASK)) |
		((v & USB_EPR_STAT_TX_MASK) ^ USB_EPR_STAT_TX_VALID);
}

void usb_valid_rx(int id)
{
	int v = USB_EPR(id);
	USB_EPR(id) =
		USB_EPR_CTR_RX |
		USB_EPR_CTR_TX |
		(v & (USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK | USB_EPR_EA_MASK)) |
		((v & USB_EPR_STAT_RX_MASK) ^ USB_EPR_STAT_RX_VALID);
}

void usb_stall_rx(int id)
{
	int v = USB_EPR(id);
	USB_EPR(id) =
		USB_EPR_CTR_RX |
		USB_EPR_CTR_TX |
		(v & (USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK | USB_EPR_EA_MASK)) |
		((v & USB_EPR_STAT_RX_MASK) ^ USB_EPR_STAT_RX_STALL);
}

void usb_copyfrom(void *dest, const usbw_t *src, int len)
{
	int words = len >> 1;
	int rem = len & 1;
	int i;
	u16 *dest16 = (u16 *)dest;
	for (i = 0; i < words; i++) {
		dest16[i] = src[i];
	}
	if (rem) {
		*((u8 *)(dest16 + words)) = src[words] & 0xff;
	}
}

void usb_copyto(usbw_t *dest, const void *src, int len)
{
	int words = len >> 1;
	int rem = len & 1;
	u16 *src16 = (u16 *)src;
	int i;
	for (i = 0; i < words; i++) {
		dest[i] = src16[i];
	}
	if (rem) {
		dest[words] = src16[words] & 0xff;
	}
}

int usb_send_descriptors(const u8 ** d, int restart_byte, int n_descriptor, int max_length)
{
	if (tx_pending[CONTROL_ENDPOINT]) {
		dprint_s("USB: ERROR TX pending\r\n");
		return -1;
	}
	volatile usbw_t *packet = USB_PMA_PTR(USB_EP(CONTROL_ENDPOINT).tx_addr);
	int descriptor_idx = 0;
	int bytes_to_send = 0;
	int total_bytes_sent = 0;
	const u8 *descriptor_ptr = 0;
	int descriptor_bytes_remaining = 0;
	unsigned short val = 0;
	tx_pending[CONTROL_ENDPOINT] = 1;
	for (total_bytes_sent = 0; (descriptor_idx < n_descriptor|| descriptor_bytes_remaining> 0) && bytes_to_send < max_length; total_bytes_sent++) {
		if (descriptor_bytes_remaining == 0) {
			descriptor_ptr = (const u8 *)d[descriptor_idx];
			descriptor_bytes_remaining = descriptor_ptr[0];
			descriptor_idx++;
		}
		if (total_bytes_sent >= restart_byte) {
			val += ((u16)(*descriptor_ptr)) << ((bytes_to_send & 1)<<3);
			if (bytes_to_send & 1) {
				packet[bytes_to_send>>1] = val;
				val = 0;
			}
			bytes_to_send++;
		}
		descriptor_ptr++;
		descriptor_bytes_remaining--;
	}

	if (bytes_to_send & 1)
		packet[bytes_to_send>>1] = val;
	tx_next[CONTROL_ENDPOINT] = 0;
	tx_end[CONTROL_ENDPOINT] = 0;
	USB_EP(CONTROL_ENDPOINT).tx_count = bytes_to_send;
	usb_valid_tx(CONTROL_ENDPOINT);
	if (descriptor_idx == n_descriptor && descriptor_bytes_remaining == 0)
		return -1;
	else
		return total_bytes_sent;
}

void usb_send_bytes(int ep, const u8 *data, int length)
{
	if (tx_pending[ep]) {
		dprint_s("USB: ERROR TX pending\r\n");
		return;
	}
	volatile usbw_t *packet = USB_PMA_PTR(USB_EP(ep).tx_addr);
	u16 val = 0;
	int i = 0;
	tx_pending[ep] = 1;
	for (i = 0; i < length; i++) {
		val += ((u16)(data[i])) << ((i & 1)<<3);
		if (i & 1) {
			packet[i>>1] = val;
			val = 0;
		}
	}
	if (i & 1)
		packet[i>>1] = val;
	int tlen = length;
	if (length > tx_max[ep]) {
		tlen = tx_max[ep];
	}
	tx_next[ep] = ((u8 *)data) + tlen;
	tx_end[ep] = ((u8 *)data) + length;
	USB_EP(ep).tx_count = tlen;
	usb_valid_tx(ep);
}

void usb_send_words(int ep, const u16 *data, int length)
{
	if (tx_pending[ep]) {
		dprint_s("USB: ERROR TX pending\r\n");
		return;
	}
	volatile usbw_t *packet = USB_PMA_PTR(USB_EP(ep).tx_addr);
	tx_pending[ep] = 1;
	for (int i = 0; i < (length+1)/2; i++) {
		packet[i] = data[i];
	}
	int tlen = length;
	if (length > tx_max[ep]) {
		tlen = tx_max[ep];
	}
	tx_next[ep] = ((u8 *)data) + tlen;
	tx_end[ep] = ((u8 *)data) + length;
	USB_EP(ep).tx_count = tlen;
	usb_valid_tx(ep);
}

void usb_set_device_configuration(int conf_id, int length)
{
	dprint_s("USB: SET CONFIGURATION ");
	dprint_dec(conf_id);
	dprint_s("\r\n");

	u32 addr = BTABLE_SIZE + CTRL_TX_SIZE + CTRL_RX_SIZE;

	USB_EP(KEYBOARD_ENDPOINT).tx_addr = addr;
	USB_EP(KEYBOARD_ENDPOINT).tx_count = 0;
	USB_EPR(KEYBOARD_ENDPOINT) =
		USB_EPR_TYPE_INTERRUPT |
		USB_EPR_STAT_TX_NAK |
		KEYBOARD_ENDPOINT;
	addr += KEYBOARD_PACKET_SIZE;

#if USE_STORAGE
	USB_EP(STORAGE_TX_ENDPOINT).tx_addr = addr;
	USB_EP(STORAGE_TX_ENDPOINT).tx_count = 0;
	USB_EPR(STORAGE_TX_ENDPOINT) =
		USB_EPR_TYPE_BULK |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_TX_NAK |
		USB_EPR_STAT_RX_DISABLED |
		STORAGE_TX_ENDPOINT;
	addr += STORAGE_TX_SIZE;

	USB_EP(STORAGE_RX_ENDPOINT).rx_addr = addr;
	USB_EP(STORAGE_RX_ENDPOINT).rx_count = USB_EP_RX_COUNT(STORAGE_RX_SIZE);
	USB_EPR(STORAGE_RX_ENDPOINT) =
		USB_EPR_TYPE_BULK |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_RX_VALID |
		USB_EPR_STAT_TX_DISABLED |
		STORAGE_RX_ENDPOINT;
	addr += STORAGE_RX_SIZE;
#else
	USB_EP(CDC_ACM_ENDPOINT).tx_addr = addr;
	USB_EP(CDC_ACM_ENDPOINT).tx_count = 0;
	USB_EPR(CDC_ACM_ENDPOINT) =
		USB_EPR_TYPE_INTERRUPT |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_RX_DISABLED |
		USB_EPR_STAT_TX_NAK |
		CDC_ACM_ENDPOINT;
	addr += CDC_ACM_PACKET_SIZE;

	USB_EP(CDC_RX_ENDPOINT).rx_addr = addr;
	USB_EP(CDC_RX_ENDPOINT).rx_count = USB_EP_RX_COUNT(CDC_RX_SIZE);
	USB_EPR(CDC_RX_ENDPOINT) =
		USB_EPR_TYPE_BULK |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_RX_VALID |
		USB_EPR_STAT_TX_DISABLED |
		CDC_RX_ENDPOINT;
	addr += CDC_RX_SIZE;

	USB_EP(CDC_TX_ENDPOINT).tx_addr = addr;
	USB_EP(CDC_TX_ENDPOINT).tx_count = 0;
	USB_EPR(CDC_TX_ENDPOINT) =
		USB_EPR_TYPE_BULK |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_TX_NAK |
		USB_EPR_STAT_RX_DISABLED |
		CDC_TX_ENDPOINT;
	addr += CDC_TX_SIZE;
#endif
	USB_EP(RAW_HID_TX_ENDPOINT).tx_addr = addr;
	USB_EP(RAW_HID_TX_ENDPOINT).tx_count = 0;
	USB_EPR(RAW_HID_TX_ENDPOINT) =
		USB_EPR_TYPE_INTERRUPT |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_TX_NAK |
		USB_EPR_STAT_RX_DISABLED |
		RAW_HID_TX_ENDPOINT;
	addr += RAW_HID_TX_SIZE;

	USB_EP(RAW_HID_RX_ENDPOINT).rx_addr = addr;
	USB_EP(RAW_HID_RX_ENDPOINT).rx_count = USB_EP_RX_COUNT(RAW_HID_RX_SIZE);
	USB_EPR(RAW_HID_RX_ENDPOINT) =
		USB_EPR_TYPE_INTERRUPT |
		USB_EPR_KIND_SINGLE_BUFFER |
		USB_EPR_STAT_TX_DISABLED |
		USB_EPR_STAT_RX_VALID |
		RAW_HID_RX_ENDPOINT;
	addr += RAW_HID_RX_SIZE;
}

int usb_tx_pending(int ep)
{
	return tx_pending[ep];
}

static void usb_ctm()
{
	while (USB_ISTR & USB_ISTR_CTR) {
		int id = USB_ISTR & USB_ISTR_ID_MASK;
		if (USB_ISTR & USB_ISTR_DIR) {
			volatile usbw_t *data = USB_PMA_PTR(USB_EP(id).rx_addr);
			int count = USB_EP(id).rx_count & ((1<<10)-1);
			int setup = USB_EPR(id) & USB_EPR_SETUP;
			//Clear CTR_RX
			USB_EPR(id) = USB_EPR_CTR_TX |
				(USB_EPR(id) & (USB_EPR_EA_MASK | USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK));
			usb_rx(id, setup, data, count);
		} else {
			if(!tx_pending[id]) {
				dprint_s("USB: ERROR spurious TX\r\n");
				return;
			}
			//Clear CTR_TX
			tx_pending[id] = 0;

			USB_EPR(id) = USB_EPR_CTR_RX |
				(USB_EPR(id) & (USB_EPR_EA_MASK | USB_EPR_TYPE_MASK | USB_EPR_KIND_MASK));
			if (tx_next[id] != tx_end[id]) {
				int length = tx_end[id] - tx_next[id];
				usb_send_bytes(id, tx_next[id], length);
			} else {
				switch (id) {
				case CONTROL_ENDPOINT:
					if (usb_device_addr) {
						USB_DADDR = USB_DADDR_EN | usb_device_addr;
					}
					break;
				default:
					break;
				}
				usb_tx(id);
			}
		}
	}
}

static void usb_pmaovr()
{
	dprint_s("USB: PMAOVR\r\n");
}

static void usb_err()
{
	dprint_s("USB: ERR\r\n");
}

void usb_reset()
{
	dprint_s("USB: RESET\r\n");
	usb_device_addr = 0;
	USB_CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM | USB_CNTR_ERRM | USB_CNTR_PMAOVRM;
	USB_BTABLE = 0;
	USB_EP(CONTROL_ENDPOINT).tx_addr = BTABLE_SIZE;
	USB_EP(CONTROL_ENDPOINT).tx_count = 0;
	USB_EP(CONTROL_ENDPOINT).rx_addr = BTABLE_SIZE + CTRL_TX_SIZE;
	USB_EP(CONTROL_ENDPOINT).rx_count = USB_EP_RX_COUNT(CTRL_RX_SIZE);
	USB_EPR(CONTROL_ENDPOINT) =
		USB_EPR_TYPE_CONTROL |
		USB_EPR_KIND_NO_STATUS_OUT |
		USB_EPR_STAT_RX_VALID |
		USB_EPR_STAT_TX_NAK;
	USB_DADDR = USB_DADDR_EN; //Enable USB interface
#if defined(MCU_STM32F303XC)
	USB_PULLUP_PORT->BSRR = (1<<USB_PULLUP_PIN); //Enable internal pullup
#else
	USB_BCDR |= USB_BCDR_DPPU; //Enable internal pullup
#endif
}

void usb_handler()
{
	unsigned int istr = USB_ISTR;
	if (istr & USB_ISTR_RESET)
		usb_reset();
	if (istr & USB_ISTR_PMAOVR)
		usb_pmaovr();
	if (istr & USB_ISTR_ERR)
		usb_err();
	if (istr & USB_ISTR_CTR)
		usb_ctm();
	USB_ISTR = USB_ISTR & ~istr;
#ifdef USB_EXTI_LINE
	EXTI_PR = 1<<USB_EXTI_LINE;
#endif
}
