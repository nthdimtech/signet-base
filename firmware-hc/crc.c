#include "crc.h"

#include "stm32f7xx.h"

void crc_init()
{
}

u32 crc_32(const u32 *din, int count)
{
	CRC->CR = CRC_CR_RESET;
	for (int i = 0; i < count; i++) {
		CRC->DR = *din;
	}
	return CRC->DR;
}
