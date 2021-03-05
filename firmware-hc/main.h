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
#define MMC_RX_CPLT_WORK (1<<4)
#define READ_DB_TX_CPLT_WORK (1<<5)
#define FLASH_WORK (1<<6)
#define USBD_SCSI_CRYPT_WORK (1<<7)
#define SYNC_ROOT_BLOCK_WORK (1<<8)
#define BUTTON_PRESS_WORK (1<<9)
#define BUTTON_PRESSING_WORK (1<<10)
#define TIMER_WORK (1<<11)
#define BLINK_WORK (1<<12)
#define WORK_STATUS_WORK (1<<13)
#define WRITE_TEST_TX_WORK (1<<14)
#define READ_TEST_TX_WORK (1<<15)

#if ENABLE_MMC_STANDBY
#define MMC_IDLE_WORK (1<<15)
#endif

extern volatile int g_work_to_do;

#define NESTED_ISR_LOCK_NEEDED() uint32_t isr_status_temp
#define NESTED_ISR_LOCK() do { \
	isr_status_temp = __get_PRIMASK(); \
	__disable_irq(); \
} while (0)
#define NESTED_ISR_UNLOCK() do { if (isr_status_temp) { __enable_irq(); }} while (0)

#define BEGIN_WORK(w) do {\
		__disable_irq();\
		g_work_to_do |= w; \
		__enable_irq();\
	} while (0)

#define END_WORK(w) do {\
		__disable_irq();\
		g_work_to_do &= ~w;\
		__enable_irq();\
	} while (0)

#define HAS_WORK(w) ((g_work_to_do & (w)) != 0)

#endif
