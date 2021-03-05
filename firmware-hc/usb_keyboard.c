#include "usbd_multi.h"
#include "usbd_hid.h"
#include "usb_keyboard.h"
#include "print.h"
#include "main.h"

volatile static struct {
	int typing;
	u8 n_chars;
	u8 *chars;
	u8 char_pos;
	int ms_type;
	int char_pos_to_type;
} s_usb_keyboard;

#define TYPE_RATE_MS 25

void usb_keyboard_type(u8 *chars_, u8 n)
{
	int ms_count = HAL_GetTick();
	if (s_usb_keyboard.typing)
		return;
	if (n == 0) {
		usb_keyboard_typing_done();
		return;
	}
	s_usb_keyboard.n_chars = n;
	s_usb_keyboard.chars = chars_;
	s_usb_keyboard.char_pos = 0;
	s_usb_keyboard.typing = 1;
	BEGIN_WORK(KEYBOARD_WORK);
	usb_send_bytes(HID_KEYBOARD_EPIN_ADDR, s_usb_keyboard.chars, 2);
	s_usb_keyboard.ms_type = ms_count;
	s_usb_keyboard.char_pos_to_type = -1;
	s_usb_keyboard.char_pos++;
}

int usb_keyboard_idle_ready()
{
	return s_usb_keyboard.typing;
}

void usb_keyboard_idle()
{
	int ms_count = HAL_GetTick();
	if (s_usb_keyboard.typing && s_usb_keyboard.char_pos_to_type >= 0 && ms_count > (s_usb_keyboard.ms_type + TYPE_RATE_MS)) {
		usb_send_bytes(HID_KEYBOARD_EPIN_ADDR, s_usb_keyboard.chars + (s_usb_keyboard.char_pos_to_type * 4), 2);
		s_usb_keyboard.ms_type = HAL_GetTick();
		s_usb_keyboard.char_pos_to_type = -1;
		s_usb_keyboard.char_pos++;
	}
}

void usb_tx_keyboard()
{
	if (s_usb_keyboard.typing) {
		if (s_usb_keyboard.char_pos == s_usb_keyboard.n_chars) {
			s_usb_keyboard.typing = 0;
			END_WORK(KEYBOARD_WORK);
			usb_keyboard_typing_done();
		} else {
			s_usb_keyboard.char_pos_to_type = s_usb_keyboard.char_pos;
		}
	} else {
		dprint_s("Unexpected keyboard TX\r\n");
	}
}

