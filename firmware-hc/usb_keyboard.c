#include "usbd_multi.h"
#include "usbd_hid.h"
#include "usb_keyboard.h"
#include "print.h"
#include "main.h"

volatile int typing = 0;
static u8 n_chars;
static u8 *chars;
static u8 char_pos;
static volatile int ms_type = 0;
static volatile int char_pos_to_type;

#define TYPE_RATE_MS 25

void usb_keyboard_type(u8 *chars_, u8 n)
{
	int ms_count = HAL_GetTick();
	if (typing)
		return;
	if (n == 0) {
		usb_keyboard_typing_done();
		return;
	}
	n_chars = n;
	chars = chars_;
	char_pos = 0;
	typing = 1;
	led_on();
	usb_send_bytes(HID_KEYBOARD_EPIN_ADDR, chars, 2);
	ms_type = ms_count;
	char_pos_to_type = -1;
	char_pos++;
}

void usb_keyboard_idle()
{
	int ms_count = HAL_GetTick();
	if (typing && char_pos_to_type >= 0 && ms_count > (ms_type + TYPE_RATE_MS)) {
		usb_send_bytes(HID_KEYBOARD_EPIN_ADDR, chars + (char_pos_to_type * 4), 2);
		ms_type = HAL_GetTick();
		char_pos_to_type = -1;
		char_pos++;
	}
}

void usb_tx_keyboard()
{
	if (typing) {
		if (char_pos == n_chars) {
			typing = 0;
			led_off();
			usb_keyboard_typing_done();
		} else {
			char_pos_to_type = char_pos;
		}
	} else {
		dprint_s("Unexpected keyboard TX\r\n");
	}
}

