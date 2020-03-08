#include "print.h"
#include "commands.h"
#include "types.h"
#include "stm32f7xx.h"

#ifdef BOOT_MODE_B

#ifdef ENABLE_FIDO2
#include "fido2/ctap.h"
#endif

#include "rand.h"

//HC_TODO: This implemention is very similar to rtc_rand.c.
// Need to figure out how to share code with it.
static u32 rng_rand_buf[1024];
static int rng_rand_head = 0;

static int rng_rand_level = 0;
static int rng_rand_tail = 0;

static int rng_rand_rewind_level = 0;
static int rng_rand_rewind_tail = -1;

void rng_init()
{
	RNG->CR = RNG_CR_RNGEN | RNG_CR_IE;
}

int rng_rand_avail(void)
{
	return rng_rand_level;
}

void rng_rand_set_rewind_point(void)
{
	rng_rand_rewind_tail = rng_rand_tail;
	rng_rand_rewind_level = rng_rand_level;
}

void rng_rand_clear_rewind_point(void)
{
	rng_rand_rewind_tail = -1;
	RNG->CR |= RNG_CR_IE;
}

void rng_rand_rewind()
{
	if (rng_rand_rewind_tail >= 0) {
		rng_rand_level = rng_rand_rewind_level;
		rng_rand_tail = rng_rand_rewind_tail;
	}
}

void RNG_IRQHandler()
{
	if (RNG->SR & RNG_SR_DRDY) {
		u32 r = RNG->DR;
		int next_head = (rng_rand_head + 1) % 1024;
		int tail = (rng_rand_rewind_tail < 0) ?
			rng_rand_tail : rng_rand_rewind_tail;
		if (next_head == tail) {
			RNG->CR &= ~RNG_CR_IE;
			return;
		}
		rng_rand_head = next_head;	
		rng_rand_buf[next_head] ^= r;
		rng_rand_level++;
		if (rng_rand_rewind_tail >= 0) {
			rng_rand_rewind_level++;
		}
		rand_update(RAND_SRC_TRNG);
	} else {
		RNG->SR |= RNG_SR_CECS;
		RNG->SR |= RNG_SR_SECS;
	}
}

u32 rng_rand_get()
{
	if (rng_rand_head == rng_rand_tail)
		return 0;
	u32 ret = rng_rand_buf[rng_rand_tail];
	rng_rand_tail = (rng_rand_tail + 1) % 1024;
	rng_rand_level--;
	if (rng_rand_rewind_tail < 0) {
		RNG->CR |= RNG_CR_IE;
	}
	return ret;
}
#endif
