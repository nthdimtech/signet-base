#ifndef FLASH_H
#define FLASH_H
#include "types.h"

void flash_write_page(u8 *dest, const u8 *src, int count);
void flash_write(u8 *dest, const u8 *src, int count);
u32 flash_sector_to_addr(int x);
int flash_addr_to_sector(u32 addr);
void flash_idle();
void flash_unlock();
int flash_writing();

enum hc_boot_mode {
	HC_BOOT_BOOTLOADER_MODE,
	HC_BOOT_APPLICATION_MODE,
	HC_BOOT_UNKNOWN_MODE
};
enum hc_boot_mode flash_get_boot_mode();
void flash_set_boot_mode(enum hc_boot_mode mode);

#endif
