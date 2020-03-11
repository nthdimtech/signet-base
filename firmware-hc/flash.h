#ifndef FLASH_H
#define FLASH_H
#include "types.h"
#include "signetdev_hc_common.h"

void flash_write_page(u8 *dest, const u8 *src, int count);
int flash_write(u8 *dest, const u8 *src, int count);
u32 flash_sector_to_addr(int x);
int flash_addr_to_sector(u32 addr);
void flash_idle();
int flash_idle_ready();
void flash_unlock();
int flash_writing();
int is_flash_idle();

enum hc_boot_mode flash_get_boot_mode();
void flash_set_boot_mode(enum hc_boot_mode mode);

#endif
