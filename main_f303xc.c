#include "types.h"
#include "regmap.h"
#include "usart.h"
#include "hid_keyboard.h"
#include "usb.h"
#include "print.h"
#include "flash.h"
#include "usb_keyboard.h"
#include "stm_aes.h"
#include "rtc_rand.h"
#include "gpio.h"
#include "irq.h"

#include "config.h"

#define PLLCLK 48000000
#define SYSCLK PLLCLK
#define HCLK (SYSCLK/HCLK_DIVISOR)
#define SYSTICK HCLK
#define PCLK HCLK

volatile int ms_count = 0;
static int blink_state = 0;
static int blink_start = 0;
static int blink_period = 0;
static int blink_duration = 0;

void blink_timeout();

void led_on()
{
	blink_state = 1;
	STATUS_LED_PORT->BSRR = 1 << (STATUS_LED_PIN);
}

void led_off()
{
	blink_state = 0;
	STATUS_LED_PORT->BSRR = 1 << (STATUS_LED_PIN + 16);
}

void blink_idle()
{
	if (blink_period > 0) {
		int next_blink_state = ((ms_count - blink_start)/(blink_period/2)) % 2;
		if (next_blink_state != blink_state) {
			if (next_blink_state) {
				led_off();
			} else {
				led_on();
			}
		}
		blink_state = next_blink_state;
		if (ms_count > (blink_start + blink_duration) && blink_duration > 0) {
			blink_period = 0;
			led_off();
			dprint_s("BLINK TIMEOUT\r\n");
			blink_timeout();
		}
	}
}

void start_blinking(int period, int duration)
{
	blink_start = ms_count;
	blink_period = 500;
	blink_duration = duration;
	led_on();
}

void stop_blinking()
{
	blink_period = 0;
	led_off();
}

int is_blinking()
{
	return blink_period > 0;
}

void systick_handler()
{
	ms_count++;
}

void delay(int ms)
{
	int ms_target = ms_count + ms;
	while(ms_count < ms_target);
}
int button_state = 0;

void button_press(int button_state);

void BUTTON_HANDLER()
{
	int current_button_state = (BUTTON_PORT->IDR & (1<<BUTTON_PIN)) ? 0 : 1;
	if (current_button_state != button_state) {
		button_state = current_button_state;
		dprint_s("Button state ");
		dprint_dec(button_state);
		dprint_s("\r\n");
		button_press(button_state);
	}
	EXTI_PR = (1<<BUTTON_PIN);
}

int main()
{
	RCC_CR |= RCC_CR_HSEON;
	while (!(RCC_CR & RCC_CR_HSERDY));

	RCC_CFGR = (RCC_CFGR &
		~(RCC_CFGR_PLLMUL_MASK |
		RCC_CFGR_PLLSRC_MASK |
		RCC_CFGR_USBPRE_MASK |
		RCC_CFGR_HPRE_MASK)) |
			(RCC_CFGR_PLLMUL_FACTOR * ((PLLCLK/HSECLK) - 2)) |
			RCC_CFGR_PLLSRC_HSE_PREDIV |
			RCC_CFGR_USBPRE_DIV_1 |
			(RCC_CFGR_HPRE_FACTOR * (7 + HCLK_DIVISOR_LOG2));

	RCC_CR |= RCC_CR_PLLON;
	while (!(RCC_CR & RCC_CR_PLLRDY));
	RCC_CFGR = (RCC_CFGR & ~(RCC_CFGR_SW_MASK)) |
		RCC_CFGR_SW_PLL;
	while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL);

	STK_RVR = SYSTICK/1000;
	STK_CVR = 0;
	STK_CSR = 7;
	//Set SysTick priority to zero
	SCB_SHPR3 = SCB_SHPR3 & 0xffffff;

	//Enable fault handlers
	SCB_SHCSR |= 7 << 16;

	set_irq_priority(USB_HP_IRQ, 128);
	set_irq_priority(USB_LP_IRQ, 128);
	set_irq_priority(USART1_IRQ, 128);
	set_irq_priority(BUTTON_IRQ, 128);
	set_irq_priority(FLASH_IRQ, 192);
	set_irq_priority(RTC_WKUP_IRQ, 128);
	enable_irq(USB_HP_IRQ);
	enable_irq(USB_LP_IRQ);
	enable_irq(USART1_IRQ);
	enable_irq(BUTTON_IRQ);
	enable_irq(FLASH_IRQ);
	enable_irq(RTC_WKUP_IRQ);

	RCC_APB1ENR |= RCC_APB1ENR_PWREN;
	RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
	RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
	RCC_AHBENR |= RCC_AHBENR_IOP_AEN | RCC_AHBENR_IOP_BEN;

	delay(2);

	//USART2 init
	usart_init(&usart1,
		HCLK, 115200,
		PORTA,
		9 /* pin 9-10 */,
		7 /* AF7 */);

	struct print usart1_print = {
		(void *)&usart1,
		(void (*)(void *, char c))usart_print_char
	};

	dbg_print = &usart1_print;

	dprint_s("Hello world!\r\n");

	//Button pin
	gpio_set_in(BUTTON_PORT, BUTTON_PIN, 1);
	EXTI_FTSR |= (1<<BUTTON_PIN);
	EXTI_RTSR |= (1<<BUTTON_PIN);
	EXTI_IMR |= (1<<BUTTON_PIN);

	//Led pins
	gpio_set_out(STATUS_LED_PORT, STATUS_LED_PIN);

	//Enable USB pins
	gpio_set_out(USB_PULLUP_PORT, USB_PULLUP_PIN);
	gpio_enable_af(USB_DM_PORT, USB_DM_PIN, USB_DM_AF);
	gpio_enable_af(USB_DP_PORT, USB_DP_PIN, USB_DP_AF);

	if (FLASH_CR & FLASH_CR_LOCK)
		flash_unlock();
	FLASH_CR |= FLASH_CR_EOPIE | FLASH_CR_ERRIE;

	EXTI_IMR |= (1<<RTC_EXTI_LINE);
	EXTI_RTSR |= (1<<RTC_EXTI_LINE);
	PWR_CR |= PWR_CR_DPB;
	RCC_CSR |= RCC_CSR_LSION;
	while (!(RCC_CSR & RCC_CSR_LSIRDY));
	RCC_BDCR = (RCC_BDCR & ~(RCC_BDCR_RTCSEL_MASK)) |
		RCC_BDCR_RTCEN |
		RCC_BDCR_RTCSEL_LSI;
	rtc_rand_init(0xff);

	RCC_APB1ENR |= RCC_APB1ENR_USBEN; //Enable USB
	USB_CNTR = USB_CNTR_FRES; //Power on + reset
	USB_ISTR = 0;
	USB_CNTR = USB_CNTR_RESETM; //Handle reset ISR disable force reset
	dprint_s("Entering main loop\r\n");
	while(1) {
		if (!flash_writing()) {
			__asm__("wfi");
		}
		__asm__("cpsid i");
		blink_idle();
		flash_idle();
		__asm__("cpsie i");
	}
	return 0;
}
