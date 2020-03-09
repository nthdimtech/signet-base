#ifndef RTC_RAND_H
#define RTC_RAND_H

#include "types.h"

void rtc_rand_init(u16 rate);
int rtc_rand_avail();
u32 rtc_rand_get();

void rtc_rand_set_rewind_point();
void rtc_rand_rewind();
void rtc_rand_clear_rewind_point();

#define RTC_EXTI_LINE (22)

void rtc_rand_irq_enable(int en);

#endif
