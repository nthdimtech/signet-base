#include "flash.h"
#include "regmap.h"
#include "common.h"

enum flash_state {
	FLASH_IDLE,
	FLASH_ERASING,
	FLASH_WRITING
};

enum flash_state flash_state = FLASH_IDLE;
u16 *flash_write_data = NULL;
u16 *flash_write_addr = NULL;
u16 flash_write_count = 0;

void flash_unlock()
{
	FLASH_KEYR = FLASH_KEYR_KEY1;
	FLASH_KEYR = FLASH_KEYR_KEY2;
}

int flash_writing()
{
	return flash_state == FLASH_WRITING;
}

void flash_erase(u8 *addr)
{
	FLASH_CR |= FLASH_CR_PER;
	FLASH_AR = (u32)addr;
	FLASH_CR |= FLASH_CR_STRT;
	flash_state = FLASH_ERASING;
}

void flash_write_complete();
void flash_write_failed();

void flash_write_state()
{
	for (int i = 0; i < 32 && flash_write_count; i++) {
		*(flash_write_addr++) = *(flash_write_data++);
		while (FLASH_SR & FLASH_SR_BUSY);
		if (!(FLASH_SR & FLASH_SR_EOP)) {
			FLASH_CR &= ~FLASH_CR_PG;
			FLASH_CR |= FLASH_CR_EOPIE;
			flash_state = FLASH_IDLE;
			flash_write_failed();
			return;
		}
		FLASH_SR |= FLASH_SR_EOP;
		flash_write_count--;
	}
	if (!flash_write_count) {
		FLASH_CR &= ~FLASH_CR_PG;
		FLASH_CR |= FLASH_CR_EOPIE;
		flash_state = FLASH_IDLE;
		flash_write_complete();
	}
}

void flash_handler()
{
	switch(flash_state) {
	case FLASH_ERASING:
		FLASH_CR &= ~(FLASH_CR_PER);
		if (FLASH_SR & FLASH_SR_EOP) {
			FLASH_SR |= FLASH_SR_EOP;
			if (flash_write_data) { 
				FLASH_CR |= FLASH_CR_PG;
				FLASH_CR &= ~FLASH_CR_EOPIE;
				flash_state = FLASH_WRITING;
			} else {
				flash_state = FLASH_IDLE;
				flash_write_complete();
			}
		} else {
			flash_state = FLASH_IDLE;
			flash_write_failed();
		}
		break;
	default:
		break;
	}
}

void flash_idle()
{
	switch (flash_state) {
	case FLASH_WRITING:
		flash_write_state();
	default:
		break;
	}	
}

void flash_write_page(u8 *dest, u8 *src, int count)
{
	if (flash_state == FLASH_IDLE) {
		flash_write_data = (u16 *)src;
		flash_write_addr = (u16 *)dest;
		flash_write_count = (count + 1)/2;
		if (FLASH_CR & FLASH_CR_LOCK)
			flash_unlock();
		FLASH_CR |= FLASH_CR_PER;
		FLASH_AR = (u32)dest;
		FLASH_CR |= FLASH_CR_STRT;
		flash_state = FLASH_ERASING;
	}
}

void flash_write(u8 *dest, u8 *src, int count)
{
	if (flash_state == FLASH_IDLE) {
		flash_write_data = (u16 *)src;
		flash_write_addr = (u16 *)dest;
		flash_write_count = (count + 1)/2;
		if (FLASH_CR & FLASH_CR_LOCK)
			flash_unlock();
		FLASH_CR |= FLASH_CR_PG;
		FLASH_CR &= ~FLASH_CR_EOPIE;
		flash_state = FLASH_WRITING;
	}
}
