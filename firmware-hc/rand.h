#ifndef RAND_H
#define RAND_H

#include "types.h"

int rand_avail();
u32 rand_get();

void rand_set_rewind_point();
void rand_rewind();
void rand_clear_rewind_point();

enum rand_owner {
	RAND_OWNER_COMMAND,
	RAND_OWNER_CTAP,
	RAND_OWNER_NONE
};

enum rand_src {
	RAND_SRC_TRNG,
	RAND_SRC_RTC
};

int rand_begin_read(enum rand_owner owner);
void rand_end_read(enum rand_owner owner);
void rand_update(enum rand_src src);

#endif
