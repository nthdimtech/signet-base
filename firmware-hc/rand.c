#include "rand.h"

#ifdef BOOT_MODE_B

#include "rtc_rand.h"
#include "rng_rand.h"
#include "fido2/ctap.h"
#include "commands.h"
#include "stm32f733xx.h"

struct rand_src_state rng_rand_state;
struct rand_src_state rtc_rand_state;

void rand_init()
{
	rng_rand_state.rewind_tail = -1;
	rtc_rand_state.rewind_tail = -1;
}

static void rand_src_rewind(struct rand_src_state *src)
{
	if (src->rewind_tail >= 0) {
		src->level = src->rewind_level;
		src->tail = src->rewind_tail;
	}
}

void rand_rewind()
{
	rand_src_rewind(&rng_rand_state);
	rand_src_rewind(&rtc_rand_state);
}

int rand_avail()
{
	int rtc_level = rtc_rand_state.level;
	int rng_level = rng_rand_state.level;
	return (rtc_level > rng_level) ? rng_level : rtc_level;
}

static u32 rand_src_get(struct rand_src_state *src)
{
	if (src->head == src->tail)
		return 0;
	u32 ret = src->buf[src->tail];
	src->tail = (src->tail + 1) % 1024;
	src->level--;
	return ret;
}

u32 rand_get()
{
	u32 rtc_val = rand_src_get(&rtc_rand_state);
	u32 rng_val = rand_src_get(&rng_rand_state);
	rng_rand_irq_enable(1);
	rtc_rand_irq_enable(1);
	return rtc_val ^ rng_val;
}

static void rand_src_rewind_point(struct rand_src_state *src)
{
	src->rewind_tail = src->tail;
	src->rewind_level = src->level;
}

void rand_set_rewind_point()
{
	rand_src_rewind_point(&rtc_rand_state);
	rand_src_rewind_point(&rng_rand_state);
}

void rand_clear_rewind_point()
{
	rtc_rand_state.rewind_tail = -1;
	rng_rand_state.rewind_tail = -1;
	rng_rand_irq_enable(1);
	rtc_rand_irq_enable(1);
}

static int s_rand_owner = RAND_OWNER_NONE;
static int s_rand_command_waiting = 0;
static int s_rand_ctap_waiting = 0;

void rand_push(enum rand_src _src, u32 val)
{
	struct rand_src_state *src;
	switch(_src) {
	case RAND_SRC_TRNG:
		src = &rng_rand_state;
		break;
	case RAND_SRC_RTC:
		src = &rtc_rand_state;
		break;
	}
	int next_head = (src->head + 1) % 1024;
	int tail = (src->rewind_tail < 0) ? src->tail : src->rewind_tail;
	if (next_head != tail) {
		src->buf[src->head] ^= val;
		src->level++;
		src->head = next_head;
		if (src->rewind_tail >= 0) {
			src->rewind_level++;
		}
		rand_update(_src);
	} else {
		switch (_src) {
		case RAND_SRC_TRNG:
			rng_rand_irq_enable(0);
			break;
		case RAND_SRC_RTC:
			rtc_rand_irq_enable(0);
			break;
		}
	}
}

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
	} else if (s_rand_owner != owner) {
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
		if (s_rand_command_waiting && s_rand_owner != RAND_OWNER_COMMAND) {
			s_rand_command_waiting = 0;
			s_rand_owner = RAND_OWNER_COMMAND;
			__enable_irq();
			cmd_rand_update();
		} else if (s_rand_ctap_waiting && s_rand_owner != RAND_OWNER_CTAP) {
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
