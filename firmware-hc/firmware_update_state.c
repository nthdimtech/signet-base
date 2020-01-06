#include <memory.h>

#include "stm32f7xx_hal.h"
#include "firmware_update_state.h"
#include "commands.h"
#include "signetdev_common.h"
#include "flash.h"
#include "memory_layout.h"

void update_firmware_cmd(u8 *data, int data_len)
{
	//TODO: Load firmware info
}

void update_firmware_cmd_complete()
{
	finish_command_resp(OKAY);
	enter_state(DS_FIRMWARE_UPDATE);
}

int in_memory_range(u32 addr, u32 base_addr, u32 len)
{
	u32 end_addr = base_addr + len - 1;
	return (addr >= base_addr && addr <= end_addr);
}

void firmware_update_write_block_complete()
{
	switch (g_device_state) {
	case DS_ERASING_PAGES:
		cmd_data.erase_flash_pages.index++;
		g_progress_level[0] = cmd_data.erase_flash_pages.index;
		get_progress_check();
		if (cmd_data.erase_flash_pages.index == cmd_data.erase_flash_pages.max_page) {
			enter_state(DS_FIRMWARE_UPDATE);
		} else {
			u8 *addr = (u8 *)flash_sector_to_addr(cmd_data.erase_flash_pages.index);
			flash_write_page(addr, NULL, 0);
		}
		break;
	default:
		break;
	}
}

static void erase_flash_pages_cmd(u8 *data, int data_len)
{
	int i;
	enum hc_boot_mode mode = flash_get_boot_mode();
	switch (mode) {
	case HC_BOOT_BOOTLOADER_MODE:
		cmd_data.erase_flash_pages.index = 5;
		cmd_data.erase_flash_pages.min_page = 5;
		cmd_data.erase_flash_pages.max_page = 7;
		break;
	case HC_BOOT_APPLICATION_MODE:
		cmd_data.erase_flash_pages.index = 2;
		cmd_data.erase_flash_pages.min_page = 2;
		cmd_data.erase_flash_pages.max_page = 4;
		break;
	default:
		finish_command_resp(INVALID_STATE);
		break;
	}
	u8 *addr = (u8 *)flash_sector_to_addr(cmd_data.erase_flash_pages.index);
	flash_write_page(addr, NULL, 0);
	int temp[] = {cmd_data.erase_flash_pages.max_page -
		cmd_data.erase_flash_pages.min_page + 1};
	enter_progressing_state(DS_ERASING_PAGES, 1, temp);
	finish_command_resp(OKAY);
}

static void write_flash_cmd(u8 *data, int data_len)
{
	enum hc_boot_mode mode = flash_get_boot_mode();
	int write_addr_base = 0;
	int write_addr_max = 0;
	int write_addr_len = 0;
	switch (mode) {
	case HC_BOOT_BOOTLOADER_MODE:
		write_addr_base = BOOT_AREA_A;
		write_addr_len = HC_BOOT_AREA_A_LEN;
		break;
	case HC_BOOT_APPLICATION_MODE:
		write_addr_base = BOOT_AREA_B;
		write_addr_len = HC_BOOT_AREA_B_LEN;
		break;
	default:
		finish_command_resp(INVALID_STATE);
		break;
	}

	u8 *dest = (u8 *)(data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24));
	data += 4;
	data_len -= 4;
	if (!in_memory_range((u32)dest, write_addr_base, write_addr_len) ||
		!in_memory_range((u32)(dest + data_len - 1), write_addr_base, write_addr_len)) {
		finish_command_resp(INVALID_INPUT);
	} else {
		flash_write(dest, data, data_len);
	}
}

static void reset_device_cmd(u8 *data, int data_len)
{
	HAL_NVIC_SystemReset();
}

static void switch_boot_mode_cmd(u8 *data, int data_len)
{
	enum hc_boot_mode mode = flash_get_boot_mode();
	switch (mode) {
	case HC_BOOT_BOOTLOADER_MODE:
		//TODO: Check CRC and possibly signature of firmware bytes written
		flash_set_boot_mode(HC_BOOT_APPLICATION_MODE);
		break;
	case HC_BOOT_APPLICATION_MODE:
		//TODO: Check CRC and possibly signature of firmware bytes written
		flash_set_boot_mode(HC_BOOT_BOOTLOADER_MODE);
		break;
	default:
		return;
	}
	HAL_NVIC_SystemReset();
}

int firmware_update_state(int cmd, u8 *data, int data_len)
{
	switch(cmd) {
	case ERASE_FLASH_PAGES:
		erase_flash_pages_cmd(data, data_len);
		break;
	case WRITE_FLASH:
		write_flash_cmd(data, data_len);
		break;
	case SWITCH_BOOT_MODE:
		switch_boot_mode_cmd(data, data_len);
		break;
	case RESET_DEVICE:
		reset_device_cmd(data, data_len);
		break;
	default:
		return -1;
	}
	return 0;
}

int erasing_pages_state(int cmd, u8 *data, int data_len)
{
	switch(cmd) {
	case GET_PROGRESS:
		get_progress_cmd(data, data_len);
		break;
	default:
		return -1;
	}
	return 0;
}
