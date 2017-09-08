#ifndef CONFIG_L433_CC_H
#define CONFIG_L433_CC_H
#include "types.h"
#include "regmap.h"

#define HW_REV 4

#if HW_REV == 1

#define BUTTON_PIN 7
#define BUTTON_PORT PORTA
#define BUTTON_PORT_NUM 0
#define BUTTON_HANDLER ext_9_5_handler
#define BUTTON_IRQ EXT_9_5_IRQ

#define STATUS_LED_COUNT 1

#define STATUS_LED1_PIN 1
#define STATUS_LED1_PORT PORTC

#elif HW_REV == 2

#define BUTTON_PIN 13
#define BUTTON_PORT PORTA
#define BUTTON_PORT_NUM 0
#define BUTTON_HANDLER ext_15_10_handler
#define BUTTON_IRQ EXT_15_10_IRQ

#define STATUS_LED_COUNT 1

#define STATUS_LED1_PIN 8
#define STATUS_LED1_PORT PORTA

#elif HW_REV == 3

#define BUTTON_PIN 5
#define BUTTON_PORT PORTB
#define BUTTON_PORT_NUM 1
#define BUTTON_HANDLER ext_9_5_handler
#define BUTTON_IRQ EXT_9_5_IRQ

#define STATUS_LED_COUNT 1

#define STATUS_LED1_PIN 4
#define STATUS_LED1_PORT PORTA

#elif HW_REV == 4

#define BUTTON_PIN 7
#define BUTTON_PORT PORTB
#define BUTTON_PORT_NUM 1
#define BUTTON_HANDLER ext_9_5_handler
#define BUTTON_IRQ EXT_9_5_IRQ

#define STATUS_LED_COUNT 2

#define STATUS_LED1_PIN 6
#define STATUS_LED1_PORT PORTA

#define STATUS_LED2_PIN 6
#define STATUS_LED2_PORT PORTB

#endif

#define USB_DM_PORT PORTA
#define USB_DM_PIN 11
#define USB_DM_AF 10

#define USB_DP_PORT PORTA
#define USB_DP_PIN 12
#define USB_DP_AF 10

#define MSICLK 48000000
#define SYSCLK MSICLK
#define HCLK_DIVISOR 4
#define HCLK_DIVISOR_LOG2 2
#define HCLK (SYSCLK/HCLK_DIVISOR)
#define SYSTICK HCLK
#define PCLK HCLK
#endif
