#include "flash.h"
#include "signetdev_common_priv.h"
#include "stm32f7xx_hal.h"

#include "memory_layout.h"
#include "main.h"
#include "config.h"

enum flash_state {
	FLASH_IDLE = 0,
	FLASH_ERASING,
	FLASH_WRITING
};

static struct {
	enum flash_state state;
	int erase_sector;
	u32 write_dest;
	const u8 *write_src;
	int write_length;
} s_flash;

__weak void flash_write_complete(u32 error)
{
}

int is_flash_idle()
{
	if (s_flash.state == FLASH_IDLE) {
		return 1;
	} else {
		return 0;
	}
}

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
	switch (s_flash.state) {
	case FLASH_IDLE:
		return 0;
	default:
		return 1;
	}
}

//HC_TODO: compute these from memory layout defines
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

int flash_idle_ready()
{
	return (s_flash.state != FLASH_IDLE) ? 1 : 0;
}

void flash_idle()
{
	uint32_t bs;
	FLASH_EraseInitTypeDef et;
	int status;

	switch (s_flash.state) {
	case FLASH_IDLE:
		break;
	case FLASH_ERASING:
		HAL_FLASH_Unlock();
		et.TypeErase = FLASH_TYPEERASE_SECTORS;
		et.Sector = s_flash.erase_sector;
		et.NbSectors = 1;
		et.VoltageRange = FLASH_VOLTAGE_RANGE_3;
		status = HAL_FLASHEx_Erase(&et, &bs);
		assert(status == HAL_OK);
		if (status != HAL_OK) {
			s_flash.state = FLASH_IDLE;
			END_WORK(FLASH_WORK);
			HAL_FLASH_Lock();
			flash_write_complete(status);
		} else if (s_flash.write_length) {
			s_flash.state = FLASH_WRITING;
		} else {
			s_flash.state = FLASH_IDLE;
			END_WORK(FLASH_WORK);
			HAL_FLASH_Lock();
			flash_write_complete(0);
		}
		break;
	case FLASH_WRITING:
		for (int i = 0; i < 16 && s_flash.write_length > 0; i++) {
#ifdef VCC_1_8
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, s_flash.write_dest, *s_flash.write_src);
			s_flash.write_length--;
			s_flash.write_src++;
			s_flash.write_dest++;
#else
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, s_flash.write_dest, *((u32 *)s_flash.write_src));
			assert(status == HAL_OK);
			s_flash.write_length-=4;
			s_flash.write_src+=4;
			s_flash.write_dest+=4;
#endif
			if (status != HAL_OK) {
				s_flash.state = FLASH_IDLE;
				END_WORK(FLASH_WORK);
				HAL_FLASH_Lock();
				flash_write_complete(status);
				return;
			}
		}
		if (s_flash.write_length == 0) {
			s_flash.state = FLASH_IDLE;
			END_WORK(FLASH_WORK);
			HAL_FLASH_Lock();
			flash_write_complete(0);
		}
		break;
	}
}

int flash_write (u8 *dest, const u8 *src, int count)
{
	if (s_flash.state == FLASH_IDLE) {
		s_flash.write_dest = (u32)dest;
		s_flash.write_src = src;
		s_flash.write_length = count;
		assert((s_flash.write_length & 3) == 0);
		HAL_FLASH_Unlock();
		s_flash.state = FLASH_WRITING;
		BEGIN_WORK(FLASH_WORK);
		return 1;
	} else {
		assert(0);
	}
	return 0;
}

void flash_write_page (u8 *dest, const u8 *src, int count)
{
	if (s_flash.state == FLASH_IDLE) {
		s_flash.write_dest = (u32)dest;
		s_flash.write_src = src;
		s_flash.write_length = count;
		assert((s_flash.write_length & 3) == 0);
		s_flash.erase_sector = flash_addr_to_sector((u32)dest);
		s_flash.state = FLASH_ERASING;
		BEGIN_WORK(FLASH_WORK);
	} else {
		assert(0);
	}
}
