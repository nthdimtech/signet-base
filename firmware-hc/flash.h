#ifndef FLASH_H
#define FLASH_H
#include "types.h"

void flash_write_page(u8 *dest, u8 *src, int count);
u32 flash_sector_to_addr(int x);
int flash_addr_to_sector(u32 addr);
void flash_idle();
void flash_unlock();
int flash_writing();
#endif
