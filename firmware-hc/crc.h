#ifndef CRC_H
#define CRC_H

#include "signetdev_common.h"

void crc_init();

u32 crc_32(const u8 *din, int count);

#endif
