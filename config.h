#ifndef CONFIG_H
#define CONFIG_H
#include "types.h"
#include "regmap.h"

#if defined(MCU_STM32F303XC)
#include "config_f303xc.h"
#elif defined(MCU_STM32L443XC)
#include "config_l443xc.h"
#else
#error Unknown MCU
#endif

#endif
