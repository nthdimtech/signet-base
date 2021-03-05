#include "rand.h"

#ifdef BOOT_MODE_B

#include "rtc_rand.h"
#include "rng_rand.h"
#include "fido2/ctap.h"
#include "commands.h"
#include "stm32f733xx.h"

static struct rand_src_state s_rng_rand_state;
static struct rand_src_state s_rtc_rand_state;

void rand_init()
{
	s_rng_rand_state.rewind_tail = -1;
	s_rtc_rand_state.rewind_tail = -1;
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
	rand_src_rewind(&s_rng_rand_state);
	rand_src_rewind(&s_rtc_rand_state);
}

int rand_avail()
{
	int rtc_level = s_rtc_rand_state.level;
	int rng_level = s_rng_rand_state.level;
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
	u32 rtc_val = rand_src_get(&s_rtc_rand_state);
	u32 rng_val = rand_src_get(&s_rng_rand_state);
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
	rand_src_rewind_point(&s_rtc_rand_state);
	rand_src_rewind_point(&s_rng_rand_state);
}

void rand_clear_rewind_point()
{
	s_rtc_rand_state.rewind_tail = -1;
	s_rng_rand_state.rewind_tail = -1;
	rng_rand_irq_enable(1);
	rtc_rand_irq_enable(1);
}

void rand_push(enum rand_src _src, u32 val)
{
	struct rand_src_state *src;
	switch(_src) {
	case RAND_SRC_TRNG:
		src = &s_rng_rand_state;
		break;
	case RAND_SRC_RTC:
		src = &s_rtc_rand_state;
		break;
	default:
		return;
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

	switch (device_subsystem_owner()) {
	case SIGNET_SUBSYSTEM:
		cmd_rand_update();
		break;
#ifdef ENABLE_FIDO2
	case CTAP_SUBSYSTEM:
		ctap_rand_update();
		break;
#endif
	default:
		break;
	}
}

#endif
