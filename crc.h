#ifndef CRC_H
#define CRC_H

#include "signetdev/common/signetdev_common.h"

void crc_init();

u32 crc_32(const u32 *din, int count);

#endif
