#include "regmap.h"

void set_irq_priority(int irq, int priority)
{
	int n = irq / 4;
	int m = irq % 4;
	NVIC_IPRn(n) = (NVIC_IPRn(n) & ~(0xff<<(m*8))) | (priority << (m * 8));
}

void enable_irq(int irq)
{
	NVIC_ISERn(irq / 32) |= 1<<(irq % 32);
}
