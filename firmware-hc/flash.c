#include "flash.h"
#include "signetdev_common_priv.h"
#include "stm32f7xx_hal.h"

#include "memory_layout.h"
#include "main.h"
#include "config.h"

enum flash_state {
	FLASH_IDLE,
	FLASH_ERASING,
	FLASH_WRITING
};

enum flash_state flash_state = FLASH_IDLE;

__weak void flash_write_complete()
{
}


static int flash_erase_sector;
static u32 flash_write_dest;
static const u32 *flash_write_src;
static int flash_write_length;

u32 flash_sector_to_addr(int x)
{
	uint32_t val = FLASH_MEM_BASE_ADDR;
	if (x > 0) val += 0x4000;
	if (x > 1) val += 0x4000;
	if (x > 2) val += 0x4000;
	if (x > 3) val += 0x4000;
	if (x > 4) val += 0x10000;
	if (x > 5) val += 0x20000;
	if (x > 6) val += 0x20000;
	if (x > 7) val += 0x20000;
	return val;
}

int flash_addr_to_sector(u32 addr)
{
	u32 addr_i = (u32)addr;
	for (u32 i = 0; i < 8; i++) {
		u32 start = flash_sector_to_addr(i);
		u32 end = flash_sector_to_addr(i+1);
		if (addr_i >= start && addr_i < end) {
			return i;
		}
	}
	return -1;
}


void flash_unlock()
{
}

int flash_writing()
{
	switch (flash_state) {
	case FLASH_IDLE:
		return 0;
	default:
		return 1;
	}
}

//TODO: compute these from memory layout defines'
#define BOOTLOADER_MODE_ADD0_VAL (0x82)
#define APPLICATION_MODE_ADD0_VAL (0x88)

void flash_set_boot_mode(enum hc_boot_mode mode)
{
	HAL_FLASH_Unlock();
	HAL_FLASH_OB_Unlock();
	FLASH_OBProgramInitTypeDef obInit;
	obInit.OptionType = OPTIONBYTE_BOOTADDR_0;
	switch (mode) {
	case HC_BOOT_BOOTLOADER_MODE:
		obInit.BootAddr0 = BOOTLOADER_MODE_ADD0_VAL;
		break;
	case HC_BOOT_APPLICATION_MODE:
		obInit.BootAddr0 = APPLICATION_MODE_ADD0_VAL;
		break;
	default:
		//What to do here?
		return;
	}
	HAL_FLASHEx_OBProgram(&obInit);
	HAL_FLASH_OB_Launch();
	HAL_FLASH_OB_Lock();
	HAL_FLASH_Lock();
}

enum hc_boot_mode flash_get_boot_mode()
{
	FLASH_OBProgramInitTypeDef obInit;
	obInit.OptionType = OPTIONBYTE_BOOTADDR_0;
	HAL_FLASHEx_OBGetConfig(&obInit);
	switch (obInit.BootAddr0) {
	case BOOTLOADER_MODE_ADD0_VAL:
		return HC_BOOT_BOOTLOADER_MODE;
	case APPLICATION_MODE_ADD0_VAL:
		return HC_BOOT_APPLICATION_MODE;
	}
	return HC_BOOT_UNKNOWN_MODE;
}

void flash_idle()
{
	uint32_t bs;
	FLASH_EraseInitTypeDef et;
	int status;

	switch (flash_state) {
	case FLASH_IDLE:
		break;
	case FLASH_ERASING:
		HAL_FLASH_Unlock();
		et.TypeErase = FLASH_TYPEERASE_SECTORS;
		et.Sector = flash_erase_sector;
		et.NbSectors = 1;
		et.VoltageRange = FLASH_VOLTAGE_RANGE_3;
		status = HAL_FLASHEx_Erase(&et, &bs);
		if (status != HAL_OK) {
			while(1);
		}
		if (flash_write_length) {
			flash_state = FLASH_WRITING;
		} else {
			flash_state = FLASH_IDLE;
			HAL_FLASH_Lock();
			flash_write_complete();
		}
		break;
	case FLASH_WRITING:
		for (int i = 0; i < 16 && flash_write_length > 0; i++) {
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_write_dest, *flash_write_src);
			assert(status == HAL_OK);
			flash_write_length -= 4;
			flash_write_src++;
			flash_write_dest += 4;
		}
		if (flash_write_length == 0) {
			flash_state = FLASH_IDLE;
			HAL_FLASH_Lock();
			flash_write_complete();
		}
		break;
	}
}

int flash_write (u8 *dest, const u8 *src, int count)
{
	if (flash_state == FLASH_IDLE) {
		flash_write_dest = (u32)dest;
		flash_write_src = (u32 *)src;
		flash_write_length = count;
		assert((flash_write_length & 3) == 0);
		HAL_FLASH_Unlock();
		flash_state = FLASH_WRITING;
		return 1;
	} else {
		assert(0);
	}
	return 0;
}

void flash_write_page (u8 *dest, const u8 *src, int count)
{
	if (flash_state == FLASH_IDLE) {
		flash_write_dest = (u32)dest;
		flash_write_src = (u32 *)src;
		flash_write_length = count;
		assert((flash_write_length & 3) == 0);
		flash_erase_sector = flash_addr_to_sector((u32)dest);
		flash_state = FLASH_ERASING;
	} else {
		assert(0);
	}
}
