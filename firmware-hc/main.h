#ifndef MAIN_H
#define MAIN_H

#include "stm32f7xx_hal.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_multi.h"

#define LOW_INT_PRIORITY (3)
#define DEFAULT_INT_PRIORITY (2)
#define HIGH_INT_PRIORITY (1)
#define HIGHEST_INT_PRIORITY (0)

#define ENABLE_MMC_STANDBY 0

void led_on();
void led_off();
void start_blinking(int period, int duration);
void stop_blinking();
void pause_blinking();
void resume_blinking();
int is_blinking();
void timer_start(int ms);
void timer_stop();
#ifndef assert
void assert(int cont);
#endif
void assert_lit(int cont, int l1, int l2);
void Error_Handler();

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

int is_ctap_initialized();

#define KEYBOARD_WORK (1<<0)
#define READ_DB_TX_WORK (1<<1)
#define WRITE_DB_TX_WORK (1<<2)
#define MMC_TX_CPLT_WORK (1<<3)
#define MMC_TX_DMA_CPLT_WORK (1<<4)
#define MMC_RX_CPLT_WORK (1<<5)
#define READ_DB_TX_CPLT_WORK (1<<6)
#define FLASH_WORK (1<<7)
#define USBD_SCSI_WORK (1<<8)
#define SYNC_ROOT_BLOCK_WORK (1<<9)
#define BUTTON_PRESS_WORK (1<<10)
#define BUTTON_PRESSING_WORK (1<<11)
#define TIMER_WORK (1<<12)
#define BLINK_WORK (1<<13)

#if ENABLE_MMC_STANDBY
#define MMC_IDLE_WORK (1<<14)
#endif

extern volatile int g_work_to_do;

#define BEGIN_WORK(w) g_work_to_do |= w
#define END_WORK(w) do {\
		__disable_irq();\
		g_work_to_do &= ~w;\
		__enable_irq();\
	} while(0)

#endif
