#include "regmap.h"

void gpio_enable_af(volatile struct gpio_port *port, int pin, int af)
{
	port->MODER = (port->MODER & ~(0x3 << (pin*2))) | (0x2 << (pin*2));
	if (af >= 8) {
		port->AFRH = (port->AFRH & ~(0xf << ((pin - 8) * 4))) | (af << ((pin - 8)*4));
	} else {
		port->AFRL = (port->AFRL & ~(0xf << (pin * 4))) | (af << (pin*4));
	}
}

void gpio_set_out(volatile struct gpio_port *port, int pin)
{
	port->MODER = (port->MODER & ~(3 << (pin*2))) | (1 << (pin*2));
}

void gpio_set_in(volatile struct gpio_port *port, int pin, int pullup)
{
	port->MODER = (port->MODER & ~(3 << (pin*2)));
	if (pullup) {
		port->PUPDR = (port->PUPDR & ~(3 << (pin * 2))) | (1 << (pin * 2));
	}
}

void gpio_out_set(volatile struct gpio_port *port, int pin)
{
	port->BSRR = 1<<pin;
}

void gpio_out_clear(volatile struct gpio_port *port, int pin)
{
	port->BSRR = 1<<(pin +16);
}
