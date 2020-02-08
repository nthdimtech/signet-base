#include "bootloader_state.h"
#include "firmware_update_state.h"
#include "signetdev_common_priv.h"

int bootloader_state(int active_cmd, u8 *data, int data_len)
{
	switch (active_cmd) {
	case SWITCH_BOOT_MODE:
		switch_boot_mode_cmd(data, data_len);
		break;
	case UPDATE_FIRMWARE:
		update_firmware_cmd(data, data_len);
		update_firmware_cmd_complete();
		break;
	default:
		return -1;
	}
	return 0;
}
