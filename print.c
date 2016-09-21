#include "print.h"

const char hex_lookup[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void print_s(struct print *p, char *str)
{
	while(*str) {
		PRINT_CHAR(p, *str);
		str++;
	}
}

void print_dec(struct print *p, u32 val)
{
	int pow_10;
	int leading = 1;
	for(pow_10 = 10000000; pow_10 > 0; pow_10 /= 10) {
		int didget = val / pow_10;
		if (didget == 0 && leading)
			continue;
		leading = 0;
		PRINT_CHAR(p, hex_lookup[didget]);
		val -= pow_10 * didget;
	}
	if (leading)
		PRINT_CHAR(p, '0');
}

void print_hex(struct print *p, unsigned int val)
{
	print_s(p, "0x");
	int i=0;

	int leading = 1;
	for (i = 0; i < 8; i++) {
		int s = (7-i)*4;
		int n = (val & (0xf << s)) >> s;
		if (n == 0 && leading)
			continue;
		leading = 0;
		PRINT_CHAR(p, hex_lookup[n]);
	}
	if (leading)
		PRINT_CHAR(p, '0');
}

void null_print_char(void *data, char c)
{
}

struct print null_print = {
	NULL,
	null_print_char
};

struct print *dbg_print = &null_print;

