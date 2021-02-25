#ifndef FIRMWARE_UPDATE_STATE_H
#define FIRMWARE_UPDATE_STATE_H

#include "types.h"

int firmware_update_state(int cmd, u8 *data, int data_len);
int erasing_pages_state(int cmd, u8 *data, int data_len);

void firmware_update_write_block_complete(u32 error);

void switch_boot_mode_cmd(u8 *data, int data_len);
void update_firmware_cmd(u8 *data, int data_len);
void update_firmware_cmd_complete();
void write_flash_cmd_complete(u32 rc);
#endif
