#ifndef GPIO_H
#define GPIO_H
void gpio_enable_af(volatile struct gpio_port *port, int pin, int af);
void gpio_set_out(volatile struct gpio_port *port, int pin);
void gpio_set_in(volatile struct gpio_port *port, int pin, int pullup);
void gpio_out_set(volatile struct gpio_port *port, int pin);
void gpio_out_clear(volatile struct gpio_port *port, int pin);
#endif
