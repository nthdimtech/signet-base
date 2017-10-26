#include "startup.h"
#include "print.h"

extern unsigned long _data_flash;
extern unsigned long _data_begin;
extern unsigned long _data_end;
extern unsigned long _bss_begin;
extern unsigned long _bss_end;
extern unsigned long _stack_end;

void handler_default(void)
{
	dprint_s("Unhandled IRQ/fault\r\n");
	while(1);
}

void systick_handler(void) __attribute__ ((weak, alias ("handler_default")));
void usb_handler(void) __attribute__ ((weak, alias ("handler_default")));
void usart1_handler(void) __attribute__ ((weak, alias ("handler_default")));
void usart2_handler(void) __attribute__ ((weak, alias ("handler_default")));
void usart3_handler(void) __attribute__ ((weak, alias ("handler_default")));
void ext_15_10_handler(void) __attribute__ ((weak, alias ("handler_default")));
void ext4_handler(void) __attribute__ ((weak, alias ("handler_default")));
void ext3_handler(void) __attribute__ ((weak, alias ("handler_default")));
void i2c1_handler(void) __attribute__ ((weak, alias ("handler_default")));
void i2c2_handler(void) __attribute__ ((weak, alias ("handler_default")));
void flash_handler(void) __attribute__ ((weak, alias ("handler_default")));
void rtc_alarm(void) __attribute__ ((weak, alias ("handler_default")));
void rtc_wakeup(void) __attribute__ ((weak, alias ("handler_default")));
void spi1_handler(void) __attribute__ ((weak, alias ("handler_default")));
void spi2_handler(void) __attribute__ ((weak, alias ("handler_default")));
void sdmmc_handler(void) __attribute__ ((weak, alias ("handler_default")));
void rng_handler(void) __attribute__ ((weak, alias ("handler_default")));
void ext_9_5_handler(void) __attribute__ ((weak, alias ("handler_default")));

__attribute__ ((section(".interrupt_vector")))
void (* const table_interrupt_vector[])(void) =
{
	(void *) &_stack_end, // 0 - stack
	handler_reset, // 1 = reset
	handler_default, // 2 NMI
	handler_hard, // 3 Failt
	handler_mem, // 4 mem management
	handler_bus, // 5 bus fault
	handler_usage, // 6 usage fault
	handler_default, //7
	handler_default, // 8
	handler_default, // 9
	handler_default, // 10
	handler_default, // 11 SVCall
	handler_default, // 12
	handler_default, // 13
	handler_default, // 14 PendSV
	systick_handler, // 15  SysTick
	//peripherals
	handler_default, // 0 WWDG
	handler_default, // 1 PVD
	handler_default, // 2
	rtc_wakeup, // 3
	flash_handler, // 4
	handler_default, // 5
	handler_default, // 6
	handler_default, // 7
	handler_default, // 8
	ext3_handler, // 9
	ext4_handler, // 10
	handler_default, // 11
	handler_default, // 12
	handler_default, // 13
	handler_default, // 14
	handler_default, // 15
	handler_default, // 16
	handler_default, // 17
	handler_default, // 18
	handler_default, // 19
	handler_default, // 20
	handler_default, // 21
	handler_default, // 22
	ext_9_5_handler, // 23
	handler_default, // 24
	handler_default, // 25
	handler_default, // 26
	handler_default, // 27
	handler_default, // 28
	handler_default, // 29
	handler_default, // 30
	i2c1_handler, // 31
	i2c1_handler, // 32
	i2c2_handler, // 33
	i2c2_handler, // 34
	spi1_handler, // 35
	spi2_handler, // 36
	usart1_handler, // 37
	usart2_handler, // 38
	usart3_handler, // 39
	ext_15_10_handler, // 40
	rtc_alarm, // 41
	handler_default, // 42
	handler_default, // 43
	handler_default, // 44
	handler_default, // 45
	handler_default, // 46
	handler_default, // 47
	handler_default, // 48
	sdmmc_handler, // 49
	handler_default, // 50
	handler_default, // 51
	handler_default, // 52
	handler_default, // 53
	handler_default, // 54
	handler_default, // 55
	handler_default, // 56
	handler_default, // 57
	handler_default, // 58
	handler_default, // 59
	handler_default, // 60
	handler_default, // 61
	handler_default, // 62
	handler_default, // 63
	handler_default, // 64
	handler_default, // 65
	handler_default, // 66
	usb_handler, // 67
	handler_default, // 78
	handler_default, // 79
	handler_default, // 70
	handler_default, // 71
	handler_default, // 72
	handler_default, // 73
	handler_default, // 74
	handler_default, // 75
	handler_default, // 76
	handler_default, // 77
	handler_default, // 78
	handler_default, // 79
	rng_handler, // 80
	handler_default, // 81
	handler_default, // 82
};
