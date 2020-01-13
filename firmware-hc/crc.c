#include "crc.h"

#include "stm32f7xx.h"

#include "types.h"

static CRC_HandleTypeDef hcrc = {
    .Instance = CRC,
    .Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE,
    .Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE,
    .Init.CRCLength = CRC_POLYLENGTH_32B,
    .Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_BYTE,
    .Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE,
    .InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES,
};

void crc_init()
{
	HAL_CRC_Init(&hcrc);
}

u32 crc_32(const u8 *din, int count)
{
	u32 crc = HAL_CRC_Calculate(&hcrc, (uint32_t *)din, count);
	return ~crc;
}

u32 crc_32_cont(const u8 *din, int count)
{
	return ~HAL_CRC_Accumulate(&hcrc, (uint32_t *)din, count);
}
