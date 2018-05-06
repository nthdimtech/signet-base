#include <memory.h>
#include <stddef.h>

#include "commands.h"
#include "signetdev/common/signetdev_common.h"
#include "types.h"
#include "usb.h"
#include "usb_keyboard.h"
#include "print.h"
#include "flash.h"

#include "stm_aes.h"
#include "firmware_update_state.h"
#include "rtc_rand.h"
#include "main.h"
#include "rng.h"
#include "usb_driver.h"

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

u8 encrypt_key[AES_256_KEY_SIZE];

u8 token_auth_rand_ct[AES_256_KEY_SIZE];
u8 token_encrypt_key_ct[AES_256_KEY_SIZE];

u8 header_version;
u8 db_version;

static const u8 root_signature[AES_BLK_SIZE] = {2 /*root block format */,3,4,5, 6,7,8,9, 10,11,12,13, 14,15,0};

static union state_data_u state_data;

static void long_button_press_disconnected();
static void button_press_disconnected();
static void button_release_disconnected();

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
			u32 crc;
			u8 db_version;
			u8 auth_rand[AES_256_KEY_SIZE];
			u8 auth_rand_ct[AES_256_KEY_SIZE];
			u8 encrypt_key_ct[AES_256_KEY_SIZE];
			u8 cbc_iv[AES_BLK_SIZE];
			u8 salt[SALT_SZ_V2];
			u8 hashfn[HASH_FN_SZ];
			struct cleartext_pass cleartext_passwords[NUM_CLEARTEXT_PASS];
		} v2;
	} header;
} __attribute__((__packed__)) root_page;

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

#define ID_BLK(id) (((u8 *)&_root_page) + BLK_SIZE * (id))

void get_progress_check();
void delete_cmd_complete();
void get_data_cmd_complete();
void set_data_cmd_complete();
void initialize_cmd_complete();
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
	cmd_resp[5] = device_state;
	if (payload) {
		memcpy(cmd_resp + CMD_PACKET_HEADER_SIZE, payload, payload_len);
	}
	if (!messages_remaining) {
		active_cmd = -1;
	}
	cmd_packet_send(cmd_resp, full_length);
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
	if (id < MIN_DATA_BLOCK || id > MAX_DATA_BLOCK) {
		finish_command_resp(ID_INVALID);
		return -1;
	}
	return 0;
}

int validate_present_id(int id)
{
	if (id < MIN_DATA_BLOCK || id > MAX_DATA_BLOCK || *((u16 *)ID_BLK(id)) != 0) {
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
	if (rand_avail() >= (INIT_RAND_DATA_SZ/4) && !cmd_data.init_data.root_block_finalized && (cmd_data.init_data.blocks_written == NUM_DATA_BLOCKS)) {
		cmd_data.init_data.root_block_finalized = 1;
		memcpy(root_page.signature, root_signature, AES_BLK_SIZE);
		for (int i = 0; i < (INIT_RAND_DATA_SZ/4); i++) {
			((u32 *)cmd_data.init_data.rand)[i] ^= rand_get();
		}
		root_page.header.v2.db_version = 2;
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
//
//

void get_rand_bits_cmd_check();
void login_cmd_iter();

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
	switch (active_cmd) {
	case GET_RAND_BITS:
		get_rand_bits_cmd_check();
		break;
	case LOGIN:
		login_cmd_iter();
		break;
	}
}

void flash_write_storage_complete();
void startup_cmd_iter();

void flash_write_complete()
{
	switch (device_state) {
	case STARTUP:
		startup_cmd_iter();
		break;
	case INITIALIZING:
		cmd_data.init_data.blocks_written++;
		progress_level[0] = cmd_data.init_data.blocks_written;
		if (progress_level[0] > progress_maximum[0]) progress_level[0] = progress_maximum[0];
		progress_level[1] = cmd_data.init_data.random_data_gathered;
		get_progress_check();

		if (cmd_data.init_data.blocks_written < NUM_DATA_BLOCKS) {
			struct block *blk = db2_initialize_block(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK, (struct block *)cmd_data.init_data.block);
			flash_write_page(ID_BLK(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK), (u8 *)blk, blk ? BLK_SIZE : 0);
		} else if (cmd_data.init_data.blocks_written == NUM_DATA_BLOCKS) {
			finalize_root_page_check();
		} else {
			dprint_s("DONE INITIALIZING\r\n");
			//TODO: fix magic numbers
			header_version = 2;
			db_version = 2;
			if (db2_startup_scan(cmd_data.init_data.block, &cmd_data.init_data.blk_info)) {
				enter_state(LOGGED_OUT);
			} else {
				//Shouldn't be possible to have a INITIAL database in an inconsistent state
				enter_state(UNINITIALIZED);
			}
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
	case WRITE_CLEARTEXT_PASSWORDS:
	case CHANGE_MASTER_PASSWORD:
	case WRITE_BLOCK:
	case ERASE_BLOCK:
	//V1 commands
	case SET_DATA:
	case DELETE_ID:
	case WRITE_FLASH:
		finish_command_resp(OKAY);
		break;
	case UPDATE_UID:
		update_uid_cmd_write_finished();
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

void cmd_packet_sent()
{
	switch(active_cmd) {
	case READ_ALL_UIDS:
		read_all_uids_cmd_iter();
		break;
	}
}

void long_button_press()
{
	if (waiting_for_long_button_press) {
		end_long_button_press_wait();
		switch(active_cmd) {
		case READ_ALL_UIDS:
			read_all_uids_cmd_iter();
			break;
		case UPDATE_FIRMWARE:
			finish_command_resp(OKAY);
			enter_state(FIRMWARE_UPDATE);
			break;
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
		case INITIALIZE:
			initialize_cmd_complete();
			break;
		case READ_CLEARTEXT_PASSWORDS:
			finish_command(OKAY, (u8 *)root_page.header.v2.cleartext_passwords,
				NUM_CLEARTEXT_PASS * CLEARTEXT_PASS_SIZE);
			break;
		case WRITE_CLEARTEXT_PASSWORDS:
			memcpy(root_page.header.v2.cleartext_passwords, cmd_data.write_cleartext_passwords.data,
					NUM_CLEARTEXT_PASS * CLEARTEXT_PASS_SIZE);
			flash_write_page(ID_BLK(0), (u8 *)&root_page, sizeof(root_page));
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
		}
	} else if (device_state == DISCONNECTED) {
		long_button_press_disconnected();
	}
}

void button_release()
{
	if (waiting_for_long_button_press) {
		resume_blinking();
	} else if (device_state == DISCONNECTED) {
		button_release_disconnected();
	}
}

int attempt_login_256(const u8 *auth_key, const u8 *auth_rand, const u8 *auth_rand_ct, const u8 *encrypt_key_ct, u8 *encrypt_key)
{
	u8 auth_rand_ct_test[AES_256_KEY_SIZE];
	stm_aes_256_encrypt_cbc(auth_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL, auth_rand, auth_rand_ct_test);
	if (memcmp(auth_rand_ct_test, auth_rand_ct, AES_256_KEY_SIZE)) {
		return 0;
	} else {
		stm_aes_256_decrypt_cbc(auth_key, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL, encrypt_key_ct, encrypt_key);
		return 1;
	}
}

void login_cmd_iter()
{
	if (cmd_data.login.authenticated) {
		if (rand_avail() >= (AES_256_KEY_SIZE/4)) {
			for (int i = 0; i < (AES_256_KEY_SIZE/4); i++) {
				cmd_data.login.token[i] = rand_get();
			}
			stm_aes_256_encrypt_cbc((u8 *)cmd_data.login.token, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
					root_page.header.v2.auth_rand, token_auth_rand_ct);
			stm_aes_256_encrypt_cbc((u8 *)cmd_data.login.token, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
					encrypt_key, token_encrypt_key_ct);
			finish_command(OKAY, (u8 *)cmd_data.login.gen_token, AES_256_KEY_SIZE);
			enter_state(LOGGED_IN);
		}
	}
}

static int cleartext_pass_index = -1;
static int cleartext_pass_chars_typed = 0;
static u8 cleartext_type_buf[128+4*4];
static int cleartext_pass_typing;

void timer_stop();
void timer_start(int ms);

static void button_press_disconnected()
{
	timer_stop();
}

static void generate_backspaces(u8 *buf, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		buf[i*4 + 0] = 0;
		buf[i*4 + 1] = 42;
		buf[i*4 + 2] = 0;
		buf[i*4 + 3] = 0;
	}
}

void timer_timeout()
{
	int index = cleartext_pass_index;
	cleartext_pass_index = -1;
	if (index >= 0) {
		generate_backspaces(cleartext_type_buf, index+1);
		cleartext_pass_typing = 1;
		usb_keyboard_type(cleartext_type_buf, (index + 1)*2);
	}
}

static void button_release_disconnected()
{
	if (!cleartext_pass_typing) {
		cleartext_pass_index = (cleartext_pass_index + 1);
		if (cleartext_pass_index == NUM_CLEARTEXT_PASS) {
			timer_timeout();
			timer_stop();
			return;
		}
		cleartext_pass_typing = 1;
		int i = cleartext_pass_chars_typed;
		generate_backspaces(cleartext_type_buf, cleartext_pass_chars_typed);
		cleartext_type_buf[i*4 + 0] = 0;
		cleartext_type_buf[i*4 + 1] = 30 + cleartext_pass_index;
		cleartext_type_buf[i*4 + 2] = 0;
		cleartext_type_buf[i*4 + 3] = 0;
		usb_keyboard_type(cleartext_type_buf, cleartext_pass_chars_typed*2 + 2);
		cleartext_pass_chars_typed = 0;
		timer_start(2000);
	}
}

static void long_button_press_disconnected()
{
	switch (root_page.signature[0]) {
	case 2: {
		int index = cleartext_pass_index;
		struct cleartext_pass *p = root_page.header.v2.cleartext_passwords + index;
		if (p->format != 0xff && p->format && p->length <= 126) {
			cleartext_pass_index = -1;
			generate_backspaces(cleartext_type_buf, index+1);
			memcpy(cleartext_type_buf + (index+1)*4, p->data, p->length*2);
			cleartext_pass_typing = 1;
			cleartext_pass_chars_typed = p->length;
			usb_keyboard_type(cleartext_type_buf, p->length + (index + 1)*2);
		} else {
			timer_timeout();
			timer_stop();
		}
	} break;
	}
}

void button_press()
{
	if (waiting_for_button_press) {
		end_button_press_wait();
		switch(active_cmd) {
		case LOGIN:
			switch (root_page.signature[0]) {
			case 2: {
				int rc = attempt_login_256(cmd_data.login.password,
						root_page.header.v2.auth_rand,
						root_page.header.v2.auth_rand_ct,
						root_page.header.v2.encrypt_key_ct,
						encrypt_key);
				if (rc) {
					if (cmd_data.login.gen_token) {
						cmd_data.login.authenticated = 1;
						login_cmd_iter();
					} else {
						finish_command_resp(OKAY);
						enter_state(LOGGED_IN);
					}
				} else {
					finish_command_resp(BAD_PASSWORD);
				}
			} break;
			}
			break;
		case LOGIN_TOKEN: {
			int rc;
			switch (root_page.signature[0]) {
			case 2:
				rc = attempt_login_256((u8 *)cmd_data.login_token.token,
					root_page.header.v2.auth_rand,
					token_auth_rand_ct,
					token_encrypt_key_ct,
					encrypt_key);
				if (rc) {
					finish_command_resp(OKAY);
					enter_state(LOGGED_IN);
				} else {
					finish_command_resp(BAD_PASSWORD);
				}
				break;
			}
			} break;
		case UPDATE_UID:
			update_uid_cmd_complete();
			break;
		case READ_UID:
			read_uid_cmd_complete();
			break;
		case BUTTON_WAIT:
			finish_command_resp(OKAY);
			break;
		}
	} else if (!waiting_for_button_press && !waiting_for_long_button_press) {
		switch (device_state) {
			case DISCONNECTED:
				button_press_disconnected();
				break;
			default:
				cmd_event_send(1, NULL, 0);
		}
	} else if (waiting_for_long_button_press) {
		pause_blinking();
	}
}

void usb_keyboard_typing_done()
{
	if (active_cmd == TYPE) {
		finish_command_resp(OKAY);
	}
	if (device_state == DISCONNECTED) {
		cleartext_pass_typing = 0;
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
	begin_long_button_press_wait();
}

void initialize_cmd_complete()
{
	cmd_data.init_data.started = 1;
	cmd_data.init_data.blocks_written = 0;
	cmd_data.init_data.random_data_gathered = 0;
	cmd_data.init_data.root_block_finalized = 0;

	struct block *blk = db2_initialize_block(MIN_DATA_BLOCK + cmd_data.init_data.blocks_written, (struct block *)cmd_data.init_data.block);
	flash_write_page(ID_BLK(MIN_DATA_BLOCK + cmd_data.init_data.blocks_written), (u8 *)blk, blk ? BLK_SIZE : 0);
	finish_command_resp(OKAY);
	cmd_data.init_data.rand_avail_init = rand_avail();
	int p = (INIT_RAND_DATA_SZ/4) - cmd_data.init_data.rand_avail_init;
	if (p < 0) p = 0;
	int temp[] = {NUM_DATA_BLOCKS, p, 1};
	enter_progressing_state(INITIALIZING, 3, temp);
}

void wipe_cmd()
{
	dprint_s("WIPE\r\n");
	begin_long_button_press_wait();
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

void restore_device_cmd()
{
	dprint_s("RESTORE DEVICE\r\n");
	begin_long_button_press_wait();
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
	begin_long_button_press_wait();
}

void cmd_disconnect()
{
	//Cancel commands waiting for button press
	end_button_press_wait();
	end_long_button_press_wait();
	active_cmd = -1;
	enter_state(DISCONNECTED);
}

void get_rand_bits_cmd_check()
{
	if (rand_avail() >= cmd_data.get_rand_bits.sz) {
		for (int i = 0; i < ((cmd_data.get_rand_bits.sz + 3)/4); i++) {
			((u32 *)cmd_data.get_rand_bits.block)[i] ^= rand_get();
		}
		finish_command(OKAY, cmd_data.get_rand_bits.block, cmd_data.get_rand_bits.sz);
	}
}

void get_rand_bits_cmd(u8 *data, int data_len)
{
	if (data_len < 2) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	cmd_data.get_rand_bits.sz = data[0] + (data[1] << 8);
	if (cmd_data.get_rand_bits.sz >= BLK_SIZE) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	get_rand_bits_cmd_check();
}

void login_cmd(u8 *data, int data_len)
{
	cmd_data.login.gen_token = 0;
	cmd_data.login.authenticated = 0;

	if (data_len < AES_256_KEY_SIZE) {
		finish_command_resp(INVALID_INPUT);
	} else {
		memcpy(cmd_data.login.password, data, AES_256_KEY_SIZE);
		data += AES_256_KEY_SIZE;
		data_len -= AES_256_KEY_SIZE;
		if (data_len > 0) {
			cmd_data.login.gen_token = data[0];
		}
		begin_button_press_wait();
	}
}

void login_token_cmd(u8 *data, int data_len)
{
	if (data_len != AES_256_KEY_SIZE) {
		finish_command_resp(INVALID_INPUT);
	} else {
		memcpy(cmd_data.login_token.token, data, AES_256_KEY_SIZE);
		begin_button_press_wait();
	}
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

void enter_mobile_mode_cmd()
{
	cmd_disconnect();
	usb_set_mobile_mode();
	usb_reconnect_device();
}

int is_device_wiped()
{
	u32 *data_area = (u32 *)(&_root_page);
	for (int i = 0; i < (NUM_STORAGE_BLOCKS*BLK_SIZE)/4; i++) {
		if (data_area[i] != 0xffffffff) {
			return 0;
		}
	}
	return 1;
}

int uninitialized_state(int cmd, u8 *data, int data_len)
{
	switch(cmd) {
	case GET_PROGRESS:
		get_progress_cmd(data, data_len);
		break;
	case RESTORE_DEVICE:
		restore_device_cmd();
		break;
	case INITIALIZE:
		initialize_cmd(data, data_len);
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
	case ENTER_MOBILE_MODE:
		enter_mobile_mode_cmd();
		break;
#ifdef FACTORY_MODE
	case UPDATE_FIRMWARE:
		finish_command_resp(OKAY);
		enter_state(FIRMWARE_UPDATE);
		break;
#else
	case UPDATE_FIRMWARE:
		if (is_device_wiped()) {
			begin_long_button_press_wait();
		} else {
			finish_command_resp(DEVICE_NOT_WIPED);
		}
		break;
#endif
	default:
		return -1;
	}
	return 0;
}

void write_cleartext_passwords(u8 *data, int data_len);

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
		begin_long_button_press_wait();
		break;
	case RESTORE_DEVICE:
		restore_device_cmd();
		break;
	case LOGIN:
		dprint_s("LOGIN\r\n");
		login_cmd(data, data_len);
		break;
	case LOGIN_TOKEN:
		dprint_s("LOGIN TOKEN\r\n");
		login_token_cmd(data, data_len);
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
	case ENTER_MOBILE_MODE:
		enter_mobile_mode_cmd();
		break;
#ifdef FACTORY_MODE
	case UPDATE_FIRMWARE:
		finish_command_resp(OKAY);
		enter_state(FIRMWARE_UPDATE);
		break;
#endif
	default:
		return -1;
	}
	return 0;
}

void write_cleartext_passwords(u8 *data, int data_len)
{
	if (data_len != (NUM_CLEARTEXT_PASS * CLEARTEXT_PASS_SIZE)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	memcpy(cmd_data.write_cleartext_passwords.data, data, data_len);
	begin_long_button_press_wait();
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
		begin_long_button_press_wait();
		break;
	case GET_RAND_BITS:
		get_rand_bits_cmd(data, data_len);
		break;
	case READ_UID: {
		if (data_len < 3) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		int uid = data[0] + (data[1] << 8);
		int masked = data[2];
		if (uid < MIN_UID || uid > MAX_UID) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		read_uid_cmd(uid, masked);
	} break;
	case UPDATE_UID: {
		if (data_len < 4) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		int uid = data[0] + (data[1] << 8);
		int sz = data[2] + (data[3] << 8);
		int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);
		data += 4;
		data_len -= 4;
		if (uid < MIN_UID || uid > MAX_UID || data_len != (blk_count * SUB_BLK_SIZE)) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		update_uid_cmd(uid, data, sz);
	} break;
	case READ_ALL_UIDS: {
		if (data_len < 1) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		int masked = data[0];
		read_all_uids_cmd(masked);
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
		begin_long_button_press_wait();
		break;
	case READ_CLEARTEXT_PASSWORDS:
		begin_long_button_press_wait();
		break;
	case WRITE_CLEARTEXT_PASSWORDS:
		write_cleartext_passwords(data, data_len);
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
		enter_state(DISCONNECTED);
		finish_command_resp(OKAY);
		break;
	default:
		return -1;
	}
	return 0;
}

void startup_cmd_iter()
{
	u8 resp[6+(HASH_FN_SZ + SALT_SZ_V2)];
	memset(resp, 0, sizeof(resp));
	resp[0] = SIGNET_MAJOR_VERSION;
	resp[1] = SIGNET_MINOR_VERSION;
	resp[2] = SIGNET_STEP_VERSION;
	resp[3] = device_state;
	resp[4] = header_version;
	resp[5] = 0;
	if (device_state == UNINITIALIZED) {
		finish_command(OKAY, resp, sizeof(resp));
	} else {
		switch (header_version) {
		case 1:
			memcpy(resp + 6, root_page.header.v1.hashfn, HASH_FN_SZ);
			memcpy(resp + 6 + HASH_FN_SZ, root_page.header.v1.salt, SALT_SZ_V1);
			db_version = 1;
			break;
		case 2:
			memcpy(resp + 6, root_page.header.v2.hashfn, HASH_FN_SZ);
			memcpy(resp + 6 + HASH_FN_SZ, root_page.header.v2.salt, SALT_SZ_V2);
			db_version = root_page.header.v2.db_version;
			break;
		}
		resp[5] = db_version;

		switch (header_version) {
		case 2:
			break;
		default:
			finish_command(UNKNOWN_DB_FORMAT, resp, sizeof(resp));
			return;
		}
		switch (db_version) {
		case 2:
			if (db2_startup_scan(cmd_data.startup.block, &cmd_data.startup.blk_info)) {
				finish_command(OKAY, resp, sizeof(resp));
			}
			break;
		default:
			finish_command(UNKNOWN_DB_FORMAT, resp, sizeof(resp));
			return;
		}
	}
}

void cmd_init()
{
	memcpy(&root_page, (u8 *)(&_root_page), sizeof(root_page));
}

void startup_cmd(u8 *data, int data_len)
{
	dprint_s("STARTUP\r\n");
	if (device_state != DISCONNECTED) {
		stop_blinking();
		end_button_press_wait();
		end_long_button_press_wait();
		active_cmd = -1;
	}
	test_state = 0;
	cmd_init();
	if (memcmp(root_page.signature + 1, root_signature + 1, AES_BLK_SIZE - 1)) {
		dprint_s("STARTUP: uninitialized\r\n");
		enter_state(UNINITIALIZED);
	} else {
		dprint_s("STARTUP: logged out\r\n");
		enter_state(LOGGED_OUT);
	}
	header_version = root_page.signature[0];
	startup_cmd_iter();
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
#ifdef TESTING_MODE
	if (waiting_for_button_press) {
		button_press();
	}
	if (waiting_for_long_button_press) {
		long_button_press();
	}
#endif
	return waiting_for_a_button_press;
}
