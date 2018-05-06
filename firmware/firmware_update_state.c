#include <memory.h>

#include "firmware_update_state.h"
#include "commands.h"
#include "signetdev/common/signetdev_common.h"
#include "flash.h"
#include "regmap.h"
#include "print.h"

int in_flash_range(u32 addr)
{
	return (addr >= FLASH_MEM_BASE_ADDR && addr <= FLASH_MEM_END_ADDR);
}

static void erase_flash_pages_cmd(u8 *data, int data_len)
{
	int i;
	for (i = 0; i < data_len; i++) {
		if (data[i] >= (FLASH_MEM_SIZE / FLASH_PAGE_SIZE)) {
			finish_command_resp(OKAY);
			return;
		}
	}
	cmd_data.erase_flash_pages.index = 0;
	cmd_data.erase_flash_pages.num_pages = data_len;
	memcpy(cmd_data.erase_flash_pages.pages, data, data_len);
	flash_write_page((void *)(FLASH_MEM_BASE_ADDR +
			FLASH_PAGE_SIZE * cmd_data.erase_flash_pages.index), NULL, 0);
	int temp[] = {cmd_data.erase_flash_pages.num_pages};
	enter_progressing_state(ERASING_PAGES, 1, temp);
	finish_command_resp(OKAY);
}

static void write_flash_cmd(u8 *data, int data_len)
{
	u8 *dest = (u8 *)(data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24));
	data += 4;
	data_len -=4;
	dprint_s("FLASH_WRITE "); dprint_hex((u32)dest); dprint_s(" "); dprint_dec(data_len); dprint_s("\r\n");
	if (!in_flash_range((u32)dest) || !in_flash_range((u32)(dest + data_len))) {
		finish_command_resp(INVALID_INPUT);
	} else {
		flash_write(dest, data, data_len);
	}
}

static void reset_device_cmd(u8 *data, int data_len)
{
	SCB_AIRCR = (SCB_AIRCR & 0xffff) | SCB_AIRCR_VECTKEY |  SCB_AIRCR_SYSRESETREQ;
	while (1);
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
