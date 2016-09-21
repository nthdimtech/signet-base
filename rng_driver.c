#include "regmap.h"
#include "print.h"
#include "commands.h"

static int rng_rand_level = 0;
static u32 rng_rand_buf[1024];
static int rng_rand_head = 0;
static int rng_rand_tail = 0;

void rng_init()
{
	RNG_CR = RNG_CR_RNGEN | RNG_CR_IE;
}

int rng_rand_avail(void)
{
	return rng_rand_level;
}

void rng_handler()
{
	if (RNG_SR & RNG_SR_DRDY) {
		u32 r = RNG_DR;
		rng_rand_buf[rng_rand_head++] ^= r;
		rng_rand_head = (rng_rand_head + 1) % 1024;
		if (rng_rand_head == rng_rand_tail) {
			rng_rand_tail = (rng_rand_tail + 1) % 1024;
			RNG_CR &= ~RNG_CR_IE;
		} else {
			rng_rand_level++;
		}
		cmd_rand_update();
	} else {
		RNG_SR |= RNG_SR_CECS;
		RNG_SR |= RNG_SR_SECS;
	}
}

u32 rng_rand_get()
{
	if (rng_rand_head == rng_rand_tail)
		return 0;
	u32 ret = rng_rand_buf[rng_rand_tail];
	rng_rand_tail = (rng_rand_tail + 1) % 1024;
	rng_rand_level--;
	RNG_CR |= RNG_CR_IE;
	return ret;
}
