#ifndef RNG_H
#define RNG_H

#include "types.h"

void rng_init();
void rng_rand_update(int avail);
int rng_rand_avail();
u32 rng_rand_get();

#endif
