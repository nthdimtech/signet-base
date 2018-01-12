#include <stddef.h>

#include "types.h"

void *memcpy(void *dest, const void *src, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		((u8 *)dest)[i] = ((const u8 *)src)[i];
	}
	return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		u8 a_d = ((const u8 *)a)[i];
		u8 b_d = ((const u8 *)b)[i];
		if (a_d != b_d)
			return 1;
	}
	return 0;
}

void *memset(void *a, int c, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		((u8 *)a)[i] = (u8) c;
	}
	return a;
}
