#ifndef FIRMWARE_UPDATE_STATE_H
#define FIRMWARE_UPDATE_STATE_H

#include "types.h"

int firmware_update_state(int cmd, u8 *data, int data_len);
int erasing_pages_state(int cmd, u8 *data, int data_len);
#endif
