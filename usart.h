#ifndef USART_H
#define USART_H

#include "regmap.h"

struct usart_if {
	char tx_buf[512];
	int tx_read_pos;
	int tx_write_pos;
	volatile struct usart_port *port;
};

extern struct usart_if usart2;

extern struct usart_if usart1;

void usart_init(struct usart_if *iface, int clock, int baud,
	volatile struct gpio_port *port,
	int pin,
	int af_num);

void usart_print_char(struct usart_if *iface, char c);

void usart2_handler();

#endif
