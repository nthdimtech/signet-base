#ifndef PRINT_H
#define PRINT_H

#include "types.h"

struct print {
	void *data;
	void (*print_char)(void *data, char c);
};

#define PRINT_CHAR(p, c) p->print_char(p->data, c)

void print_s(struct print *p, char *str);

void print_dec(struct print *p, u32 val);

void print_hex(struct print *p, unsigned int val);

extern struct print *dbg_print;

#define dprint_s(s) print_s(dbg_print, s)
#define dprint_dec(s) print_dec(dbg_print, s)
#define dprint_hex(s) print_hex(dbg_print, s)

#endif
