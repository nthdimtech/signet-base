#include "usart.h"

struct usart_if usart2 = {
	.port = USART2
};

struct usart_if usart1 = {
	.port = USART1
};

void usart_init(struct usart_if *iface, int clock, int baud,
	volatile struct gpio_port *port,
	int pin,
	int af_num)
{
	port->MODER = (port->MODER & ~(0xf << (pin*2))) | (0xA << (pin*2));
	if (pin >= 8) {
		port->AFRH = (port->AFRH & ~(0xff << (pin*4 - 32))) |
			((af_num + (af_num<<4)) << (pin*4 - 32));
	} else {
		port->AFRL = (port->AFRL & ~(0xff << (pin*4))) |
			((af_num + (af_num<<4)) << (pin*4));
	}
	iface->tx_read_pos = 0;
	iface->tx_write_pos = 0;
	iface->port->BRR = clock/baud;
	iface->port->CR1 = USART_CR1_TE | USART_CR1_UE;
}

int usart_poll(struct usart_if *iface)
{
	int empty = iface->tx_read_pos == iface->tx_write_pos;

	int txe = (iface->port->ISR & (1<<7)) == (1<<7);

	if (!empty && txe) {
		iface->port->TDR = iface->tx_buf[iface->tx_read_pos];
		iface->tx_read_pos = (iface->tx_read_pos + 1) & 0x1ff;
	}
	return empty;
}

void usart_print_char(struct usart_if *iface, char c)
{
	if (((iface->tx_write_pos + 1) & 0x1ff) == iface->tx_read_pos)
		return;
	iface->tx_buf[iface->tx_write_pos] = c;
	iface->tx_write_pos = (iface->tx_write_pos + 1) & 0x1ff;
	iface->port->CR1 |= USART_CR1_TXEIE;
}

void usart2_handler()
{
	if (usart_poll(&usart2)) {
		USART2->CR1 &= ~USART_CR1_TXEIE;
	}
}

void usart1_handler()
{
	if (usart_poll(&usart1)) {
		USART1->CR1 &= ~USART_CR1_TXEIE;
	}
}
