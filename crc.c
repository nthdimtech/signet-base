#include "regmap.h"

void crc_init()
{
}

u32 crc_32(const u32 *din, int count)
{
	CRC_CR = CRC_CR_RESET;
	for (int i = 0; i < count; i++) {
		CRC_DR = *din;
	}
	return CRC_DR;
}
