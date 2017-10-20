#include "commands.h"
#include "common.h"
#include "types.h"
#include "usb.h"
#include "usb_keyboard.h"
#include "print.h"
#include "flash.h"
#include "mem.h"
#include <stddef.h>

#include "stm_aes.h"
#include "firmware_update_state.h"
#include "rtc_rand.h"
#include "main.h"

#ifdef MCU_STM32L443XC
#include "rng.h"
#endif

//
// Globals
//

//Command code of the currently executing command or -1 if no command is executing
static int active_cmd = -1;

enum device_state device_state = DISCONNECTED;

// Incoming buffer for next command request
u8 cmd_packet_buf[CMD_PACKET_BUF_SIZE];

//Paramaters and temporary state for the command currently being
//executed
union cmd_data_u cmd_data;

static u8 encrypt_key[AES_256_KEY_SIZE];

static const u8 root_signature[AES_BLK_SIZE] = {2 /*root block format */,3,4,5, 6,7,8,9, 10,11,12,13, 14,15,0};

static union state_data_u state_data;

struct root_page
{
	u8 signature[AES_BLK_SIZE];
	union {
		struct {
			u8 auth_rand[AES_128_KEY_SIZE];
			u8 auth_rand_ct[AES_128_KEY_SIZE];
			u8 encrypt_key_ct[AES_128_KEY_SIZE];
			u8 cbc_iv[AES_BLK_SIZE];
			u8 salt[AES_128_KEY_SIZE];
			u8 hashfn[AES_BLK_SIZE];
		} v1;
		struct {
			u8 auth_rand[AES_256_KEY_SIZE];
			u8 auth_rand_ct[AES_256_KEY_SIZE];
			u8 encrypt_key_ct[AES_256_KEY_SIZE];
			u8 cbc_iv[AES_BLK_SIZE];
			u8 salt[AES_256_KEY_SIZE];
			u8 hashfn[AES_256_KEY_SIZE];
			u8 title[256];
		} v2;
	} header;
} root_page;

extern struct root_page _root_page;

static int n_progress_components = 0;
static int progress_level[8];
static int progress_maximum[8];
static int progress_check = 0;
static int progress_target_state = DISCONNECTED;
static int waiting_for_button_press = 0;
static int waiting_for_long_button_press = 0;

//
// Misc functions
//

#ifdef MCU_STM32L443XC
int rand_avail()
{
	int rtc_level = rtc_rand_avail();
	int rng_level = rng_rand_avail();
	return (rtc_level > rng_level) ? rng_level : rtc_level;
}

u32 rand_get()
{
	u32 rtc_val = rtc_rand_get();
	u32 rng_val = rng_rand_get();
	return rtc_val ^ rng_val;
}
#else
#define rand_avail() rtc_rand_avail()
#define rand_get() rtc_rand_get()
#endif

#define ID_BLK(id) (((u8 *)&_root_page) + BLK_SIZE * id)

void get_progress_check();
void delete_cmd_complete();
void get_data_cmd_complete();
void set_data_cmd_complete();

void enter_progressing_state(enum device_state state, int _n_progress_components, int *_progress_maximum)
{
	device_state = state;
	progress_check = 0;
	n_progress_components = _n_progress_components;
	int i;
	for (i = 0; i < n_progress_components; i++) {
		progress_level[i] = 0;
		progress_maximum[i] = _progress_maximum[i];
	}
	get_progress_check();
}

void enter_state(enum device_state state)
{
	enter_progressing_state(state, 0, NULL);
}

void begin_button_press_wait()
{
	waiting_for_button_press = 1;
	start_blinking(500, 10000);
}

void begin_long_button_press_wait()
{
	waiting_for_long_button_press = 1;
	start_blinking(1000, 10000);
}

void end_button_press_wait()
{
	waiting_for_button_press = 0;
	stop_blinking();
}

void end_long_button_press_wait()
{
	waiting_for_long_button_press = 0;
	stop_blinking();
}

int get_total_progress()
{
	int total = 0;
	int i;
	for (i = 0; i < n_progress_components; i++) {
		total += progress_level[i];
	}
	return total;;
}

int get_total_progress_maximum()
{
	int total = 0;
	int i;
	for (i = 0; i < n_progress_components; i++) {
		total += progress_maximum[i];
	}
	return total;
}

void get_progress_check()
{
	if (active_cmd == GET_PROGRESS) {
		int total_progress = get_total_progress();
		int total_progress_maximum = get_total_progress_maximum();
		if (progress_target_state == device_state && total_progress > progress_check) {
			u8 resp[4*(8+1)] = {
				total_progress & 0xff,
				total_progress >> 8,
				total_progress_maximum & 0xff,
				total_progress_maximum >> 8,
			};
			int i;
			for (i = 0; i < n_progress_components; i++) {
				int j = i + 1;
				resp[j * 4] = progress_level[i] & 0xff;
				resp[j * 4 + 1] = progress_level[i] >> 8;
				resp[j * 4 + 2] = progress_maximum[i] & 0xff;
				resp[j * 4 + 3] = progress_maximum[i] >> 8;
			}
			finish_command(OKAY, resp, (n_progress_components + 1) * 4);
		} else if (progress_target_state != device_state) {
			finish_command_resp(INVALID_STATE);
		}
	}
}

void finish_command_multi(enum command_responses resp, int messages_remaining, const u8 *payload, int payload_len)
{
	static u8 cmd_resp[CMD_PACKET_BUF_SIZE];
	int full_length = payload_len + CMD_PACKET_HEADER_SIZE;
	cmd_resp[0] = full_length & 0xff;
	cmd_resp[1] = (full_length >> 8) & 0xff;
	cmd_resp[2] = resp;
	cmd_resp[3] = messages_remaining & 0xff;
	cmd_resp[4] = (messages_remaining >> 8) & 0xff;
	cmd_resp[5] = 0;
	if (payload) {
		memcpy(cmd_resp + CMD_PACKET_HEADER_SIZE, payload, payload_len);
	}
	if (!messages_remaining) {
		active_cmd = -1;
	}
	if (device_state != DISCONNECTED) {
		cmd_packet_send(cmd_resp, full_length);
	}
}

void finish_command(enum command_responses resp, const u8 *payload, int payload_len)
{
	finish_command_multi(resp, 0, payload, payload_len);
}

void finish_command_resp(enum command_responses resp)
{
	finish_command(resp, NULL, 0);
}

int validate_id(int id)
{
	if (id < MIN_ID || id > MAX_ID) {
		finish_command_resp(ID_INVALID);
		return -1;
	}
	return 0;
}

int validate_present_id(int id)
{
	if (id < MIN_ID || id > MAX_ID || *((u16 *)ID_BLK(id)) != 0) {
		return -1;
	}
	return 0;
}

void derive_iv(u32 id, u8 *iv)
{
	switch(root_page.signature[0]) {
	case 1:
		memcpy(iv, root_page.header.v1.cbc_iv, AES_BLK_SIZE);
		break;
	case 2:
		memcpy(iv, root_page.header.v2.cbc_iv, AES_BLK_SIZE);
		break;
	}
	iv[AES_BLK_SIZE-1] += (u8)(id);
}


static void finalize_root_page_check()
{
	if (rand_avail() >= (INIT_RAND_DATA_SZ/4) && !cmd_data.init_data.root_block_finalized && (cmd_data.init_data.blocks_written == (MAX_ID - MIN_ID + 1))) {
		cmd_data.init_data.root_block_finalized = 1;
		memcpy(root_page.signature, root_signature, AES_BLK_SIZE);
		for (int i = 0; i < (INIT_RAND_DATA_SZ/4); i++) {
			((u32 *)cmd_data.init_data.rand)[i] ^= rand_get();
		}
		memcpy(root_page.header.v2.cbc_iv, cmd_data.init_data.rand, AES_BLK_SIZE);
		memcpy(root_page.header.v2.auth_rand, cmd_data.init_data.rand + AES_BLK_SIZE, AES_256_KEY_SIZE);
		stm_aes_256_encrypt_cbc(cmd_data.init_data.passwd, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
				root_page.header.v2.auth_rand, root_page.header.v2.auth_rand_ct);
		stm_aes_256_encrypt_cbc(cmd_data.init_data.passwd, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
				cmd_data.init_data.rand + AES_BLK_SIZE + AES_256_KEY_SIZE, root_page.header.v2.encrypt_key_ct);
		memcpy(root_page.header.v2.salt, cmd_data.init_data.salt, SALT_SZ_V2);
		memcpy(root_page.header.v2.hashfn, cmd_data.init_data.hashfn, HASH_FN_SZ);
		flash_write_page((u8 *)&_root_page, (u8 *)&root_page, sizeof(struct root_page));
	}
}

//
// System events
//

void cmd_rand_update()
{
	int avail = rand_avail();
	switch (device_state) {
	case INITIALIZING:
		if (avail <= (INIT_RAND_DATA_SZ/4)) {
			cmd_data.init_data.random_data_gathered = avail - cmd_data.init_data.rand_avail_init;
			progress_level[1] = cmd_data.init_data.random_data_gathered;
			get_progress_check();
		}
		finalize_root_page_check();
		break;
	default:
		break;
	}
}

void flash_write_storage_complete();

void flash_write_complete()
{
#if USE_STORAGE
	flash_write_storage_complete();
#endif
	switch (device_state) {
	case INITIALIZING:
		cmd_data.init_data.blocks_written++;
		progress_level[0] = cmd_data.init_data.blocks_written;
		if (progress_level[0] > progress_maximum[0]) progress_level[0] = progress_maximum[0];
		progress_level[1] = cmd_data.init_data.random_data_gathered;
		get_progress_check();

		if (cmd_data.init_data.blocks_written < (MAX_ID - MIN_ID + 1)) {
			flash_write_page(ID_BLK(cmd_data.init_data.blocks_written + MIN_ID), NULL, 0);
		} else if (cmd_data.init_data.blocks_written == (MAX_ID - MIN_ID + 1)) {
			finalize_root_page_check();
		} else {
			dprint_s("DONE INITIALIZING\r\n");
			enter_state(LOGGED_OUT);
		}
		break;
	case WIPING:
		cmd_data.wipe_data.block++;
		progress_level[0] = cmd_data.wipe_data.block;
		get_progress_check();
		if (cmd_data.wipe_data.block == NUM_STORAGE_BLOCKS) {
			enter_state(UNINITIALIZED);
		} else {
			flash_write_page(ID_BLK(cmd_data.wipe_data.block), NULL, 0);
		}
		break;
	case ERASING_PAGES:
		cmd_data.erase_flash_pages.index++;
		progress_level[0] = cmd_data.erase_flash_pages.index;
		get_progress_check();
		if (cmd_data.erase_flash_pages.index == cmd_data.erase_flash_pages.num_pages) {
			enter_state(FIRMWARE_UPDATE);
		} else {
			flash_write_page((void *)(FLASH_MEM_BASE_ADDR +
				FLASH_PAGE_SIZE * cmd_data.erase_flash_pages.index), NULL, 0);
		}
		break;
	default:
		break;
	}
	switch (active_cmd) {
	case CHANGE_MASTER_PASSWORD:
	case WRITE_BLOCK:
	case ERASE_BLOCK:
	case SET_DATA:
	case DELETE_ID:
	case WRITE_FLASH:
		finish_command_resp(OKAY);
		break;
	default:
		break;
	}
}

void flash_write_failed()
{
	if (active_cmd != -1) {
		finish_command_resp(WRITE_FAILED);
	}
	dprint_s("flash write failed\r\n");
}

void blink_timeout()
{
	if (waiting_for_button_press || waiting_for_long_button_press) {
		end_button_press_wait();
		end_long_button_press_wait();
		finish_command_resp(BUTTON_PRESS_TIMEOUT);
	}
}

void get_all_data_iter();

void cmd_packet_sent()
{
	switch(active_cmd) {
	case GET_ALL_DATA:
		if (!waiting_for_long_button_press) {
			get_all_data_iter();
		}
		break;
	}
}

void long_button_press()
{
	if (waiting_for_long_button_press) {
		end_long_button_press_wait();
		switch(active_cmd) {
		case GET_ALL_DATA:
			get_all_data_iter();
			break;
		}
	}
}

void button_release()
{
	if (waiting_for_long_button_press) {
		resume_blinking();
	}
}

void button_press()
{
	if (waiting_for_button_press) {
		end_button_press_wait();
		switch(active_cmd) {
		case LOGIN:
			switch (root_page.signature[0]) {
			case 1:
				stm_aes_128_encrypt(cmd_data.login.password, root_page.header.v1.auth_rand, cmd_data.login.cyphertext);
				if (memcmp(cmd_data.login.cyphertext, root_page.header.v1.auth_rand_ct, AES_128_KEY_SIZE)) {
					finish_command_resp(BAD_PASSWORD);
				} else {
					finish_command_resp(OKAY);
					stm_aes_128_decrypt(cmd_data.login.password, root_page.header.v1.encrypt_key_ct, encrypt_key);
					enter_state(LOGGED_IN);
				}
				break;
			case 2:
				stm_aes_256_encrypt_cbc(cmd_data.login.password, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
						root_page.header.v2.auth_rand, cmd_data.login.cyphertext);
				if (memcmp(cmd_data.login.cyphertext, root_page.header.v2.auth_rand_ct, AES_256_KEY_SIZE)) {
					finish_command_resp(BAD_PASSWORD);
				} else {
					finish_command_resp(OKAY);
					stm_aes_256_decrypt_cbc(cmd_data.login.password, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
							root_page.header.v2.encrypt_key_ct, encrypt_key);
					enter_state(LOGGED_IN);
				}
				break;
			}
			break;
		case DELETE_ID:
			delete_cmd_complete();
			break;
		case GET_DATA:
			get_data_cmd_complete();
			break;
		case SET_DATA:
			set_data_cmd_complete();
			break;
		case INITIALIZE: {
			cmd_data.init_data.started = 1;
			cmd_data.init_data.blocks_written = 0;
			cmd_data.init_data.random_data_gathered = 0;
			cmd_data.init_data.root_block_finalized = 0;
			flash_write_page(ID_BLK(MIN_ID), NULL, 0);
			finish_command_resp(OKAY);
			cmd_data.init_data.rand_avail_init = rand_avail();
			int p = (INIT_RAND_DATA_SZ/4) - cmd_data.init_data.rand_avail_init;
			if (p < 0) p = 0;
			int temp[] = {MAX_ID - MIN_ID + 1, p, 1};
			enter_progressing_state(INITIALIZING, 3, temp);
			} break;
		case WIPE: {
			finish_command_resp(OKAY);
			cmd_data.wipe_data.block = 0;
			flash_write_page(ID_BLK(cmd_data.wipe_data.block), NULL, 0);
			int temp[] = {NUM_STORAGE_BLOCKS};
			enter_progressing_state(WIPING, 1, temp);
			} break;
		case BACKUP_DEVICE:
			finish_command_resp(OKAY);
			state_data.backup.prev_state = device_state;
			enter_state(BACKING_UP_DEVICE);
			break;
		case RESTORE_DEVICE:
			finish_command_resp(OKAY);
			enter_state(RESTORING_DEVICE);
			break;
		case BUTTON_WAIT:
			finish_command_resp(OKAY);
			break;
		case CHANGE_MASTER_PASSWORD:
			switch (root_page.signature[0]) {
			case 1:
				memcpy(root_page.header.v1.hashfn, cmd_data.change_master_password.hashfn, HASH_FN_SZ);
				stm_aes_128_encrypt(cmd_data.change_master_password.new_key, encrypt_key, root_page.header.v1.encrypt_key_ct);
				stm_aes_128_encrypt(cmd_data.change_master_password.new_key, root_page.header.v1.auth_rand, root_page.header.v1.auth_rand_ct);
				memcpy(root_page.header.v1.salt, cmd_data.change_master_password.salt, SALT_SZ_V1);
				break;
			case 2:
				memcpy(root_page.header.v2.hashfn, cmd_data.change_master_password.hashfn, HASH_FN_SZ);
				stm_aes_256_encrypt_cbc(cmd_data.change_master_password.new_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
						encrypt_key, root_page.header.v2.encrypt_key_ct);
				stm_aes_256_encrypt_cbc(cmd_data.change_master_password.new_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
						root_page.header.v2.auth_rand, root_page.header.v2.auth_rand_ct);
				memcpy(root_page.header.v2.salt, cmd_data.change_master_password.salt, SALT_SZ_V2);
				break;
			}
			flash_write_page(ID_BLK(0), (u8 *)&root_page, sizeof(root_page));
			break;
		case UPDATE_FIRMWARE:
			finish_command_resp(OKAY);
			enter_state(FIRMWARE_UPDATE);
			break;
		}
	} else if (!waiting_for_button_press && !waiting_for_long_button_press) {
		cmd_event_send(1, NULL, 0);
	} else if (waiting_for_long_button_press) {
		pause_blinking();
	}
}

void usb_keyboard_typing_done()
{
	if (active_cmd == TYPE) {
		finish_command_resp(OKAY);
	}
}

//
// Command functions. These are called in response to incoming command packets
//

extern int test_state;

void initialize_cmd(u8 *data, int data_len)
{
	dprint_s("INITIALIZE\r\n");

	if (data_len < INITIALIZE_CMD_SIZE) {
		finish_command_resp(INVALID_INPUT);
		return;
	}

	u8 *d = data;

	memcpy(cmd_data.init_data.passwd, d, AES_256_KEY_SIZE); d += AES_256_KEY_SIZE;
	memcpy(cmd_data.init_data.hashfn, d, AES_BLK_SIZE); d += AES_BLK_SIZE;
	memcpy(cmd_data.init_data.salt, d, SALT_SZ_V2); d += SALT_SZ_V2;
	memcpy(cmd_data.init_data.rand, d, INIT_RAND_DATA_SZ); d += INIT_RAND_DATA_SZ;
	cmd_data.init_data.started = 0;

	data += INITIALIZE_CMD_SIZE;
	data_len -= INITIALIZE_CMD_SIZE;
	begin_button_press_wait();
}

void wipe_cmd()
{
	dprint_s("WIPE\r\n");
	begin_button_press_wait();
}

void get_progress_cmd(u8 *data, int data_len)
{
	progress_check = data[0] | (data[1] << 8);
	data += 2;
	progress_target_state = data[0] | (data[1] << 8);
	data += 2;
	if (progress_target_state != device_state) {
		finish_command_resp(INVALID_STATE);
	} else {
		get_progress_check();
	}
}

void backup_device_cmd(u8 *data, int data_len)
{
	dprint_s("BACKUP DEVICE\r\n");
	begin_button_press_wait();
}

void restore_device_cmd(u8 *data, int data_len)
{
	dprint_s("RESTORE DEVICE\r\n");
	begin_button_press_wait();
}

void read_block_cmd(u8 *data, int data_len)
{
	dprint_s("READ BLOCK\r\n");
	if (data_len != 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int idx = *data;
	if (idx >= NUM_STORAGE_BLOCKS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	finish_command(OKAY, ID_BLK(idx), BLK_SIZE);
}

void write_block_cmd(u8 *data, int data_len)
{
	dprint_s("WRITE BLOCK\r\n");
	if (data_len != (1 + BLK_SIZE)) {
		dprint_s("DATA LENGTH WRONG ");
		dprint_dec(data_len);
		dprint_s("\r\n");
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int idx = *data;
	data++;
	if (idx >= NUM_STORAGE_BLOCKS) {
		dprint_s("BAD INDEX ");
		dprint_dec(idx);
		dprint_s("\r\n");
		finish_command_resp(INVALID_INPUT);
		return;
	}
	dprint_s("IDX ");
	dprint_dec(idx);
	dprint_s("\r\n");
	flash_write_page(ID_BLK(idx), data, BLK_SIZE);
}

void erase_block_cmd(u8 *data, int data_len)
{
	dprint_s("ERASE BLOCK\r\n");
	if (data_len != 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int idx = *data;
	data++;
	if (idx >= NUM_STORAGE_BLOCKS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	flash_write_page(ID_BLK(idx), NULL, 0);
}

void get_device_capacity_cmd(u8 *data, int data_len)
{
	dprint_s("GET DEVICE CAPACITY\r\n");
	static struct {
		u16 block_size;
		u8 sub_blk_mask_size;
		u8 sub_blk_data_size;
		u16 num_blocks;
	} __attribute__((packed)) device_capacity;
	finish_command(OKAY, (u8 *)&device_capacity, sizeof(device_capacity));
}

void change_master_password_cmd(u8 *data, int data_len)
{
	dprint_s("CHANGE MASTER PASSWORD\r\n");
	if (data_len != ((AES_256_KEY_SIZE * 2) + HASH_FN_SZ + SALT_SZ_V2)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	u8 *old_key = data;
	u8 *new_key = data + AES_256_KEY_SIZE;
	u8 *hashfn = data + (AES_256_KEY_SIZE * 2);
	u8 *salt = data + (AES_256_KEY_SIZE * 2) + HASH_FN_SZ;
	memcpy(cmd_data.change_master_password.hashfn, hashfn, HASH_FN_SZ);
	memcpy(cmd_data.change_master_password.salt, salt, SALT_SZ_V2);
	switch (root_page.signature[0]) {
	case 1:
		stm_aes_128_encrypt(old_key, root_page.header.v1.auth_rand, cmd_data.change_master_password.cyphertext);
		if (memcmp(cmd_data.change_master_password.cyphertext, root_page.header.v1.auth_rand_ct, AES_128_KEY_SIZE)) {
			finish_command_resp(BAD_PASSWORD);
			return;
		}
		stm_aes_128_decrypt(old_key, root_page.header.v1.encrypt_key_ct, encrypt_key);
		memcpy(cmd_data.change_master_password.new_key, new_key, AES_128_KEY_SIZE);
		break;
	case 2:
		stm_aes_256_encrypt_cbc(old_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
				root_page.header.v2.auth_rand, cmd_data.change_master_password.cyphertext);
		if (memcmp(cmd_data.change_master_password.cyphertext, root_page.header.v2.auth_rand_ct, AES_256_KEY_SIZE)) {
			finish_command_resp(BAD_PASSWORD);
			return;
		}
		stm_aes_256_decrypt_cbc(old_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
				root_page.header.v2.encrypt_key_ct, encrypt_key);
		memcpy(cmd_data.change_master_password.new_key, new_key, AES_256_KEY_SIZE);
	}
	begin_button_press_wait();
}

static int decrypt_id(u8 *block, u8 *iv, int id, int masked)
{
	unsigned int k = 0;
	if (!validate_present_id(id)) {
		dprint_dec(id);
		dprint_s("\r\n");
		u8 *addr =  ID_BLK(id);

		u16 sz = addr[2] + (addr[3] << 8);
		int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);
		derive_iv(id, iv);
		block[k] = addr[2]; k++;
		block[k] = addr[3]; k++;
		switch (root_page.signature[0]) {
		case 1:
			stm_aes_128_decrypt_cbc(encrypt_key, blk_count, iv, addr + SUB_BLK_SIZE, block + k);
			break;
		case 2:
			stm_aes_256_decrypt_cbc(encrypt_key, blk_count, iv, addr + SUB_BLK_SIZE, block + k);
			break;
		}
		if (masked) {
			u8 *sub_block = block + k;
			for (int i = 0; i < blk_count; i++) {
				u8 *mask = sub_block;
				u8 *data = mask + SUB_BLK_MASK_SIZE;
				for (int j = 0; j < SUB_BLK_DATA_SIZE; j++) {
					int byte = j / 8;
					int bit = 1<<(j % 8);
					if (mask[byte] & bit) {
						data[j] = 0;
					}
				}
				sub_block += SUB_BLK_SIZE;
			}
		}
		k += SUB_BLK_SIZE * blk_count;
	}
	return k;
}

void get_data_cmd_complete()
{
	finish_command(OKAY, cmd_data.get_data.block, cmd_data.get_data.sz);
}

void get_data_cmd(u8 *data, int data_len)
{
	if (data_len < 2) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int id_cmd = data[0];
	int masked = data[1];
	dprint_s("GET_DATA ");
	dprint_dec(id_cmd);
	dprint_s("\r\n");
	cmd_data.get_data.id = id_cmd;
	int sz = decrypt_id(cmd_data.get_data.block, cmd_data.get_data.iv, id_cmd, masked);
	if (!sz) {
		finish_command_resp(ID_INVALID);
		return;
	}
	cmd_data.get_data.sz = sz;

	if (!masked) {
		begin_button_press_wait();
	} else {
		get_data_cmd_complete();
	}
}

void get_all_data_iter()
{
	int id = cmd_data.get_all_data.id;
	int remaining = MAX_ID - id;
	if (remaining >= 0) {
		u8 *block = cmd_data.get_all_data.block;
		block[0] = id & 0xff;
		block[1] = id >> 8;
		int sz = decrypt_id(block + 2,
			cmd_data.get_all_data.iv,
			id,
			!cmd_data.get_all_data.unmask);
		cmd_data.get_all_data.id++;
		finish_command_multi(OKAY, remaining, block, sz + 2);
	}
}

void get_all_data_cmd(u8 *data, int data_len)
{
	dprint_s("GET_ALL_DATA\r\n");
	u8 unmask;
	if (data_len != 1) {
		finish_command_resp(INVALID_INPUT);
	}
	unmask = data[0];
	cmd_data.get_all_data.id = 1;
	cmd_data.get_all_data.unmask = unmask;
	if (unmask) {
		begin_long_button_press_wait();
	} else {
		get_all_data_iter();
	}
}

void set_data_cmd_complete()
{
	flash_write_page(ID_BLK(cmd_data.set_data.id), cmd_data.set_data.block, (cmd_data.set_data.sub_blk_count + 1) * SUB_BLK_SIZE);
}

void set_data_cmd(u8 *data, int data_len)
{
	if (data_len < 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int id_cmd = data[0]; data++; data_len--;
	if (!validate_id(id_cmd)) {
		u16 sz = data[0] + (data[1] << 8);
		data += 2;
		data_len -= 2;
		int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);
		dprint_s("SET_DATA ");
		dprint_dec(id_cmd);
		dprint_s("\r\n");
		if (data_len != (blk_count * SUB_BLK_SIZE)) {
			finish_command_resp(INVALID_INPUT);
			return;
		}
		cmd_data.set_data.id = id_cmd;
		cmd_data.set_data.sub_blk_count = blk_count;
		derive_iv(id_cmd, cmd_data.set_data.iv);
		memset(cmd_data.set_data.block, 0, AES_BLK_SIZE);
		cmd_data.set_data.block[2] = sz & 0xff;
		cmd_data.set_data.block[3] = sz >> 8;
		switch (root_page.signature[0]) {
		case 1:
			stm_aes_128_encrypt_cbc(encrypt_key, blk_count, cmd_data.set_data.iv, data, cmd_data.set_data.block + SUB_BLK_SIZE);
			break;
		case 2:
			stm_aes_256_encrypt_cbc(encrypt_key, blk_count, cmd_data.set_data.iv, data, cmd_data.set_data.block + SUB_BLK_SIZE);
			break;
		}
		begin_button_press_wait();
	}
}

void delete_cmd_complete()
{
	flash_write_page(ID_BLK(cmd_data.delete_id.id), NULL, 0);
}

void delete_cmd(u8 *data, int data_len)
{
	if (data_len < 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int id_cmd = data[0];
	cmd_data.delete_id.id = id_cmd;
	if (!validate_present_id(id_cmd)) {
		dprint_s("DELETE_ID ");
		dprint_dec(id_cmd);
		dprint_s("\r\n");
		begin_button_press_wait();
	} else {
		finish_command_resp(ID_INVALID);
	}
}

void cmd_disconnect()
{
	//Cancel commands waiting for button press
	end_button_press_wait();
	end_long_button_press_wait();
	active_cmd = -1;
	enter_state(DISCONNECTED);
}

//
// State functions. These control which commands can be issued
// in different states.
//

int initializing_state(int cmd, u8 *data, int data_len)
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

int wiping_state(int cmd, u8 *data, int data_len)
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

int uninitialized_state(int cmd, u8 *data, int data_len)
{
	switch(cmd) {
	case GET_PROGRESS:
		get_progress_cmd(data, data_len);
		break;
	case RESTORE_DEVICE:
		begin_button_press_wait();
		break;
	case INITIALIZE:
		initialize_cmd(data, data_len);
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
	default:
		return -1;
	}
	return 0;
}

int logged_out_state(int cmd, u8 *data, int data_len)
{
	switch(cmd) {
	case GET_PROGRESS:
		get_progress_cmd(data, data_len);
		break;
	case CHANGE_MASTER_PASSWORD:
		change_master_password_cmd(data, data_len);
		break;
	case INITIALIZE:
		initialize_cmd(data, data_len);
		break;
	case WIPE:
		wipe_cmd();
		break;
	case BACKUP_DEVICE:
		dprint_s("BACKUP DEVICE\r\n");
		begin_button_press_wait();
		break;
	case RESTORE_DEVICE:
		dprint_s("RESTORE DEVICE\r\n");
		begin_button_press_wait();
		break;
	case LOGIN:
		dprint_s("LOGIN\r\n");
		if (data_len != AES_256_KEY_SIZE) {
			finish_command_resp(INVALID_INPUT);
		} else {
			memcpy(cmd_data.login.password, data, AES_256_KEY_SIZE);
			begin_button_press_wait();
		}
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
	default:
		return -1;
	}
	return 0;
}

int logged_in_state(int cmd, u8 *data, int data_len)
{
	switch (active_cmd) {
	case GET_PROGRESS:
		get_progress_cmd(data, data_len);
		break;
	case CHANGE_MASTER_PASSWORD:
		change_master_password_cmd(data, data_len);
		break;
	case BACKUP_DEVICE:
		dprint_s("Backup device\r\n");
		begin_button_press_wait();
		break;
	case GET_DATA:
		get_data_cmd(data, data_len);
		break;
	case GET_ALL_DATA:
		get_all_data_cmd(data, data_len);
		break;
	case SET_DATA:
		set_data_cmd(data, data_len);
		break;
	case DELETE_ID: {
		delete_cmd(data, data_len);
		break;
	} break;
	case TYPE: {
		dprint_s("TYPE\r\n");
		int n_chars = data_len >> 1;
		memcpy(cmd_data.type_data.chars, data, n_chars * 2);
		usb_keyboard_type(cmd_data.type_data.chars, n_chars);
	} break;
	case BUTTON_WAIT:
		begin_button_press_wait();
		break;
	case LOGOUT:
		enter_state(LOGGED_OUT);
		finish_command_resp(OKAY);
		break;
	case UPDATE_FIRMWARE:
		begin_button_press_wait();
		break;
	default:
		return -1;
	}
	return 0;
}

int backing_up_device_state(int cmd, u8 *data, int data_len)
{
	switch (cmd) {
	case READ_BLOCK:
		read_block_cmd(data, data_len);
		break;
	case BACKUP_DEVICE_DONE:
		enter_state(state_data.backup.prev_state);
		finish_command_resp(OKAY);
		break;
	default:
		return -1;
	}
	return 0;
}

int restoring_device_state(int cmd, u8 *data, int data_len)
{
	switch (cmd) {
	case WRITE_BLOCK:
		write_block_cmd(data, data_len);
		break;
	case ERASE_BLOCK:
		erase_block_cmd(data, data_len);
		break;
	case RESTORE_DEVICE_DONE:
		enter_state(RESET);
		finish_command_resp(OKAY);
		break;
	default:
		return -1;
	}
	return 0;
}

void startup_cmd(u8 *data, int data_len)
{
	dprint_s("STARTUP\r\n");
	if (device_state != RESET) {
		test_state = 0;
		stop_blinking();
		end_button_press_wait();
		end_long_button_press_wait();
		active_cmd = -1;
	}
	memcpy(&root_page, (u8 *)(&_root_page), BLK_SIZE);
	if (memcmp(root_page.signature + 1, root_signature + 1, AES_BLK_SIZE - 1)) {
		dprint_s("STARTUP: uninitialized\r\n");
		enter_state(UNINITIALIZED);
	} else {
		dprint_s("STARTUP: logged out\r\n");
		enter_state(LOGGED_OUT);
	}
	u8 resp[2+(HASH_FN_SZ + SALT_SZ_V2)];
	memset(resp, 0, sizeof(resp));
	resp[0] = device_state;
	resp[1] = root_page.signature[0];
	switch (root_page.signature[0]) {
	case 1:
		memcpy(resp + 2, root_page.header.v1.hashfn, HASH_FN_SZ);
		memcpy(resp + 2 + HASH_FN_SZ, root_page.header.v1.salt, SALT_SZ_V1);
		break;
	case 2:
		memcpy(resp + 2, root_page.header.v2.hashfn, HASH_FN_SZ);
		memcpy(resp + 2 + HASH_FN_SZ, root_page.header.v2.salt, SALT_SZ_V2);
		break;
	}
	switch (root_page.signature[0]) {
	case 1:
	case 2:
		finish_command(OKAY, resp, sizeof(resp));
		break;
	case 3:
		finish_command_resp(UNKNOWN_DB_FORMAT);
		break;
	}
	return;
}

int cmd_packet_recv()
{
	u8 *data = cmd_packet_buf;
	int data_len = data[0] + (data[1] << 8) - CMD_PACKET_HEADER_SIZE;
	int next_active_cmd = data[2];
	int prev_active_cmd = active_cmd;
	data += CMD_PACKET_HEADER_SIZE;

	int waiting_for_a_button_press = waiting_for_button_press | waiting_for_long_button_press;

	if (next_active_cmd == DISCONNECT) {
		cmd_disconnect();
		return waiting_for_a_button_press;
	}

	if (prev_active_cmd != -1 && next_active_cmd == CANCEL_BUTTON_PRESS && !waiting_for_a_button_press) {
		//Ignore button cancel requests with no button press waiting
		return waiting_for_a_button_press;
	}

	if (prev_active_cmd != -1 && waiting_for_a_button_press && next_active_cmd == CANCEL_BUTTON_PRESS) {
		dprint_s("CANCEL_BUTTON_PRESS\r\n");
		end_button_press_wait();
		finish_command_resp(BUTTON_PRESS_CANCELED);
		return waiting_for_a_button_press;
	}
	active_cmd = next_active_cmd;

	if (active_cmd == STARTUP) {
		startup_cmd(data, data_len);
		return waiting_for_a_button_press;
	}

	//Always allow the GET_DEVICE_STATE command. It's easiest to handle it here
	if (active_cmd == GET_DEVICE_STATE) {
		dprint_s("GET_DEVICE_STATE\r\n");
		u8 resp[] = {device_state};
		finish_command(OKAY, resp, sizeof(resp));
		return waiting_for_a_button_press;
	}
	int ret = -1;

	if (active_cmd == CANCEL_BUTTON_PRESS) {
		//If we didn't handle button cancelation press above, it could
		//be a sign of a bug
		dprint_s("CANCEL_BUTTON_PRESS misfire\r\n");
		active_cmd = -1;
		return waiting_for_a_button_press;
	}

	//Every command should be able to accept CMD_PACKET_PAYLOAD_SIZE bytes of
	//data. If there is more, we reject it here.
	if (data_len > CMD_PACKET_PAYLOAD_SIZE) {
		finish_command_resp(INVALID_INPUT);
		return waiting_for_a_button_press;
	}

	switch (device_state) {
	case DISCONNECTED:
		break;
	case UNINITIALIZED:
		ret = uninitialized_state(active_cmd, data, data_len);
		break;
	case INITIALIZING:
		ret = initializing_state(active_cmd, data, data_len);
		break;
	case WIPING:
		ret = wiping_state(active_cmd, data, data_len);
		break;
	case BACKING_UP_DEVICE:
		ret = backing_up_device_state(active_cmd, data, data_len);
		break;
	case RESTORING_DEVICE:
		ret = restoring_device_state(active_cmd, data, data_len);
		break;
	case ERASING_PAGES:
		ret = erasing_pages_state(active_cmd, data, data_len);
		break;
	case FIRMWARE_UPDATE:
		ret = firmware_update_state(active_cmd, data, data_len);
		break;
	case LOGGED_OUT:
		ret = logged_out_state(active_cmd, data, data_len);
		break;
	case LOGGED_IN:
		ret = logged_in_state(active_cmd, data, data_len);
		break;
	default:
		break;
	}
	if (ret) {
		dprint_s("INVALID STATE\r\n");
		finish_command_resp(INVALID_STATE);
	}
	return waiting_for_a_button_press;
}
