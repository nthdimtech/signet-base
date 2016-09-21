#include "rtc_rand.h"
#include "types.h"
#include "regmap.h"
#include "types.h"
#include "mem.h"
#include "print.h"
#include "commands.h"

static int rtc_rand_level = 0;

static u32 rtc_rand_buf[1024];
static int rtc_rand_head = 0;
static int rtc_rand_tail = 0;

int rtc_rand_avail(void)
{
	return rtc_rand_level;
}

u32 rtc_rand_get()
{
	if (rtc_rand_head == rtc_rand_tail)
		return 0;
	u32 ret = rtc_rand_buf[rtc_rand_tail];
	rtc_rand_tail = (rtc_rand_tail + 1) % 1024;
	rtc_rand_level--;
	return ret;
}

static u32 rndtemp = 0;
static u32 rndtemp_i = 0;

void rtc_wakeup(void)
{
	int clk = STK_CVR;
	int rnd = (clk & 1); clk>>=1;
	rnd ^= (clk & 1); clk >>=1;
	rnd ^= (clk & 1); clk >>=1;
	rnd ^= (clk & 1); clk >>=1;
	rnd ^= (clk & 1); clk >>=1;
	rndtemp <<= 1;
	rndtemp |= rnd;
	rndtemp_i++;
	//dprint_s("rtc rand "); dprint_dec(rndtemp_i); dprint_s("\r\n");
	if (rndtemp_i >= 32) {
		rndtemp_i = 0;
		rtc_rand_buf[rtc_rand_head++] ^= rndtemp;
		rtc_rand_head = (rtc_rand_head + 1) % 1024;
		if (rtc_rand_head == rtc_rand_tail) {
			rtc_rand_tail = (rtc_rand_tail + 1) % 1024;
		} else {
			rtc_rand_level++;
		}
		cmd_rand_update();
	}
	RTC_ISR &= ~(RTC_ISR_WUTF);
	EXTI_PR = 1<<RTC_EXTI_LINE;
}

void rtc_rand_init(u16 rate)
{
	RTC_WPR = RTC_WPR_KEY1;
	RTC_WPR = RTC_WPR_KEY2;

	RTC_CR &= ~RTC_CR_WUTE;
	while (!(RTC_ISR & RTC_ISR_WUTWF));
	RTC_WUTR = rate;
	RTC_ISR &= ~(RTC_ISR_WUTF);
	RTC_CR |= RTC_CR_WUTE | RTC_CR_WUTIE;
}
