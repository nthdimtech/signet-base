#include "regmap.h"
#include "print.h"

extern unsigned long _flash_origin;
extern unsigned long _data_begin;
extern unsigned long _data_end;
extern unsigned long _bss_begin;
extern unsigned long _bss_end;
extern unsigned long _stack_end;
extern unsigned long _ivt_ram;

void main();

void handler_reset(void)
{
	unsigned long *source;
	unsigned long *destination;
	// Copying data from Flash to RAM
	source = &_flash_origin;
	for (destination = &_data_begin; destination < &_data_end;)
	{
		*(destination++) = *(source++);
	}
	// default zero to undefined variables
	for (destination = &_bss_begin; destination < &_bss_end;)
	{
		*(destination++) = 0;
	}

	SCB_VTOR = (u32)&_ivt_ram;

	// starting main program
	main();
}

void handler_mem(void)
{
	dprint_s("Memory fault\r\n");
	while(1);
}

void handler_bus(void)
{
	dprint_s("Bus fault: ");
	dprint_hex(SCB_BFAR);
	dprint_s("\r\n");
	while(1);
}

void handler_usage(void)
{
	dprint_s("Usage fault\r\n");
	while(1);
}

void handler_hard(void)
{
	dprint_s("Hard fault\r\n");
	while(1);
}
