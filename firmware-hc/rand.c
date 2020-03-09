#include "rand.h"

#ifdef BOOT_MODE_B

#include "rtc_rand.h"
#include "rng_rand.h"
#include "fido2/ctap.h"
#include "commands.h"
#include "stm32f733xx.h"

void rand_rewind()
{
	rtc_rand_rewind();
	rng_rand_rewind();
}

int rand_avail()
{
	int rtc_level = rtc_rand_avail();
	int rng_level = rng_rand_avail();
	return (rtc_level > rng_level) ? rng_level : rtc_level;
}

u32 rand_get()
{
	u32 rtc_val = rtc_rand_get();
	u32 rng_val = rng_rand_get();
	return rtc_val ^ rng_val;
}

void rand_set_rewind_point()
{
	rng_rand_set_rewind_point();
	rtc_rand_set_rewind_point();
}

void rand_clear_rewind_point()
{
	rng_rand_clear_rewind_point();
	rtc_rand_clear_rewind_point();
}

static int s_rand_owner = RAND_OWNER_NONE;
static int s_rand_command_waiting = 0;
static int s_rand_ctap_waiting = 0;

void rand_update(enum rand_src src)
{
	switch (s_rand_owner) {
	case RAND_OWNER_COMMAND:
		cmd_rand_update();
		break;
#ifdef ENABLE_FIDO2
	case RAND_OWNER_CTAP:
		ctap_rand_update();
		break;
#endif
	default:
		break;
	}
}

int rand_begin_read(enum rand_owner owner)
{
	__disable_irq();
	if (s_rand_owner == RAND_OWNER_NONE) {
		s_rand_owner = owner;
	} else {
		switch (owner) {
		case RAND_OWNER_COMMAND:
			s_rand_command_waiting = 1;
			break;
		case RAND_OWNER_CTAP:
			s_rand_ctap_waiting = 1;
			break;
		default:
			break;
		}
	}
	__enable_irq();
	if (s_rand_owner == owner) {
		return 1;
	} else {
		return 0;
	}
}

void rand_end_read(enum rand_owner owner)
{
	__disable_irq();
	if (owner == s_rand_owner) {
		if (s_rand_command_waiting) {
			s_rand_command_waiting = 0;
			s_rand_owner = RAND_OWNER_COMMAND;
			__enable_irq();
			cmd_rand_update();
		} else if (s_rand_ctap_waiting) {
			s_rand_ctap_waiting = 0;
			s_rand_owner = RAND_OWNER_CTAP;
			__enable_irq();
			ctap_rand_update();
		} else {
			s_rand_owner = RAND_OWNER_NONE;
		}
	}
}

#endif
