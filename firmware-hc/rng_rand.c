#include "print.h"
#include "commands.h"
#include "types.h"
#include "stm32f7xx.h"

#ifdef BOOT_MODE_B

#ifdef ENABLE_FIDO2
#include "fido2/ctap.h"
#endif
#include "rand.h"
void rng_init()
{
	RNG->CR = RNG_CR_RNGEN | RNG_CR_IE;
}
void rng_rand_irq_enable(int en)
{
	if (en) {
		RNG->CR |= RNG_CR_IE;
	} else {
		RNG->CR &= ~RNG_CR_IE;
	}
}

void RNG_IRQHandler()
{
	if (RNG->SR & RNG_SR_DRDY) {
		rand_push(RAND_SRC_TRNG, RNG->DR);
	} else {
		RNG->SR |= RNG_SR_CECS;
		RNG->SR |= RNG_SR_SECS;
	}
}
#endif
