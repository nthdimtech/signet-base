#ifndef RTC_RAND_H
#define RTC_RAND_H

#include "types.h"

void rtc_rand_init(u16 rate);
int rtc_rand_avail();
u32 rtc_rand_get();

#define RTC_EXTI_LINE (22)

#endif
