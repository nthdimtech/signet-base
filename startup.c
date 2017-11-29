#include "regmap.h"
#include "print.h"
#include "usart.h"

extern unsigned long _flash_origin;
extern unsigned long _data_begin;
extern unsigned long _data_end;
extern unsigned long _bss_begin;
extern unsigned long _bss_end;
extern unsigned long _stack_end;
extern unsigned long _ivt_ram;

void main();

void busy_delay(int ms)
{
	int i;
	for (i = 0; i < ms; i++) {
		while (!(STK_CSR & STK_CSR_COUNT_FLAG));
		usart_poll(&usart1);
	}
}

extern void led_on();
extern void led_off();

void busy_flash_led(int count)
{
	for (int i = 0; i < count; i++) {
		led_on();
		busy_delay(200);
		led_off();
		busy_delay(200);
	}
	busy_delay(2000);
}

void handler_reset(void)
{
	unsigned long *source;
	unsigned long *destination;
	// Copying data from Flash to RAM
	source = &_flash_origin;
	for (destination = &_data_begin; destination < &_data_end;)
		*(destination++) = *(source++);

	// default zero to undefined variables
	for (destination = &_bss_begin; destination < &_bss_end;)
		*(destination++) = 0;

	SCB_VTOR = (u32)&_ivt_ram;

	// starting main program
	main();
}

void handler_mem(void)
{
	dprint_s("Memory fault\r\n");
	while (1) busy_flash_led(1);
}

void handler_bus(void)
{
	dprint_s("Bus fault: ");
	dprint_hex(SCB_BFAR);
	dprint_s("\r\n");
	while (1) busy_flash_led(2);
}

void handler_usage(void)
{
	dprint_s("Usage fault\r\n");
	while (1) busy_flash_led(3);
}

void handler_hard(void)
{
	dprint_s("Hard fault\r\n");
	while (1) busy_flash_led(4);
}
