#include <memory.h>

#include "rtc_rand.h"
#include "types.h"
#include "commands.h"
#include "stm32f7xx.h"

#ifdef BOOT_MODE_B

#ifdef ENABLE_FIDO2
#include "fido2/ctap.h"
#endif

#include "rand.h"

void rtc_rand_irq_enable(int en)
{
	if (en) {
		RTC->CR |= RTC_CR_WUTIE;
	} else {
		RTC->CR &= ~RTC_CR_WUTIE;
	}
}

void RTC_WKUP_IRQHandler(void)
{
	static u32 rndtemp = 0;
	static u32 rndtemp_i = 0;
	u32 clk = DWT->CYCCNT;
	u32 rnd = (clk & 1);
	clk>>=1;
	rnd ^= (clk & 1);
	clk >>=1;
	rnd ^= (clk & 1);
	clk >>=1;
	rnd ^= (clk & 1);
	clk >>=1;
	rnd ^= (clk & 1);
	clk >>=1;
	rndtemp <<= 1;
	rndtemp |= rnd;
	rndtemp_i++;
	if (rndtemp_i >= 32) {
		rndtemp_i = 0;
		rand_push(RAND_SRC_RTC, rndtemp);
	}
	RTC->ISR &= ~(RTC_ISR_WUTF);
	EXTI->PR = 1 << RTC_EXTI_LINE;
}

void rtc_rand_init(u16 rate)
{
	RTC->WPR = 0xCA;//RTC_WPR_KEY1;
	RTC->WPR = 0x53;//RTC_WPR_KEY2;

	RTC->CR &= ~RTC_CR_WUTE;
	while (!(RTC->ISR & RTC_ISR_WUTWF));
	RTC->CR &= ~0x3; //WUT = RTC/16 == 2Khz
	RTC->WUTR = rate;
	RTC->ISR &= ~(RTC_ISR_WUTF);
	RTC->CR |= RTC_CR_WUTE | RTC_CR_WUTIE;
}

#endif
