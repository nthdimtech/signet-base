#include "usb.h"
#include "regmap.h"
#include "usb_keyboard.h"
#include "print.h"

volatile int typing = 0;
static u8 n_chars;
static u8 *chars;
static u8 char_pos;
static volatile int ms_type = 0;
static volatile int char_pos_to_type;
extern volatile int ms_count;

#define TYPE_RATE_MS 15

void usb_keyboard_type(u8 *chars_, u8 n)
{
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

	usb_send_bytes(KEYBOARD_ENDPOINT, chars, 2);
	ms_type = ms_count;
	char_pos_to_type = -1;
	char_pos++;
}

void usb_keyboard_idle()
{
	if (typing && char_pos_to_type >= 0 && ms_count > (ms_type + TYPE_RATE_MS)) {
		usb_send_bytes(KEYBOARD_ENDPOINT, chars + (char_pos_to_type * 2), 2);
		ms_type = ms_count;
		char_pos_to_type = -1;
		char_pos++;
	}
}

void usb_tx_keyboard()
{
	if (typing) {
		if (char_pos == n_chars) {
			typing = 0;
			usb_keyboard_typing_done();
		} else {
			char_pos_to_type = char_pos;
		}
	} else {
		dprint_s("Unexpected keyboard TX\r\n");
	}
}

