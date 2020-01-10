#ifndef MAIN_H

#include "stm32f7xx_hal.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_multi.h"

#define DEFAULT_INT_PRIORITY (2)
#define HIGH_INT_PRIORITY (1)
#define HIGHEST_INT_PRIORITY (0)

void led_on();
void led_off();
void start_blinking(int period, int duration);
void stop_blinking();
void pause_blinking();
void resume_blinking();
int is_blinking();
void timer_start(int ms);
void timer_stop();

void Error_Handler();

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

#endif
