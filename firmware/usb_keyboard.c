#include "usb.h"
#include "regmap.h"
#include "usb_keyboard.h"
#include "print.h"

volatile int typing = 0;
static u8 *chars;
static u8 n_chars;
static u8 char_pos;

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
}

void usb_tx_keyboard()
{
	if (typing) {
		char_pos++;
		if (char_pos == n_chars) {
			typing = 0;
			usb_keyboard_typing_done();
		} else {
			usb_send_bytes(KEYBOARD_ENDPOINT, chars + (char_pos * 2), 2);
		}
	} else {
		dprint_s("Unexpected keyboard TX\r\n");
	}
}

