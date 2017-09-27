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

#ifdef MCU_STM32L443XC
#include "rng.h"
#endif

//
// Globals
//

//id that can be modified or have it's private data read from
static int active_id = -1;

//Command code of the currently executing command or -1 if no command is executing
static int active_cmd = -1;

enum device_state device_state = DISCONNECTED;

void led_on();
void led_off();
void start_blinking(int period, int duration);
void stop_blinking();
int is_blinking();

// Incoming buffer for next command request
u8 cmd_packet_buf[CMD_PACKET_BUF_SIZE];

//Paramaters and temporary state for the command currently being
//executed
union cmd_data_u cmd_data;

static u8 encrypt_key[AES_BLK_SIZE];

static const u8 root_signature[AES_BLK_SIZE] = {1,3,4,5,6,7,8,9,10,11,12,13,14,15,0};

static union state_data_u state_data;


struct root_page_header {
	u8 signature[AES_BLK_SIZE];
	u8 auth_rand[AES_BLK_SIZE];
	u8 auth_rand_ct[AES_BLK_SIZE];
	u8 encrypt_key_ct[AES_BLK_SIZE];
	u8 cbc_iv[AES_BLK_SIZE];
	u8 salt[AES_BLK_SIZE];
	u8 hashfn[AES_BLK_SIZE];
};


#define DEVICE_USERDATA_SIZE (BLK_SIZE - sizeof(struct root_page_header))
#define INITIALIZE_RAND_DATA_SIZE (INITIALIZE_CMD_SIZE - AES_BLK_SIZE)
#define INITIALIZE_RAND_DATA_SIZE_WORDS ((INITIALIZE_CMD_SIZE -AES_BLK_SIZE)/4)

struct root_page
{
	struct root_page_header header;
	u8 userdata[DEVICE_USERDATA_SIZE];
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

void finish_command_multi(enum command_responses resp, int packets_remaining, const u8 *payload, int payload_len)
{
	static u8 cmd_resp[CMD_PACKET_BUF_SIZE];
	int full_length = payload_len + CMD_PACKET_HEADER_SIZE;
	cmd_resp[0] = full_length & 0xff;
	cmd_resp[1] = (full_length >> 8) & 0xff;
	cmd_resp[2] = resp;
	cmd_resp[3] = packets_remaining & 0xff;
	cmd_resp[4] = (packets_remaining >> 8) & 0xff;
	cmd_resp[5] = 0;
	if (payload) {
		memcpy(cmd_resp + CMD_PACKET_HEADER_SIZE, payload, payload_len);
	}
	if (!packets_remaining) {
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
		finish_command_resp(ID_INVALID);
		return -1;
	}
	return 0;
}

void derive_iv(u32 id, u8 *iv)
{
	memcpy(iv, root_page.header.cbc_iv, AES_BLK_SIZE);
	iv[15] += (u8)(id);
}


static void finalize_root_page_check()
{
	if (rand_avail() >= INITIALIZE_RAND_DATA_SIZE_WORDS && !cmd_data.init_data.root_block_finalized && (cmd_data.init_data.blocks_written == (MAX_ID - MIN_ID + 1))) {
		cmd_data.init_data.root_block_finalized = 1;
		memcpy(root_page.header.signature, root_signature, AES_BLK_SIZE);
		memcpy(root_page.userdata, cmd_data.init_data.userdata, DEVICE_USERDATA_SIZE);
		for (int i = 0; i < INITIALIZE_RAND_DATA_SIZE_WORDS; i++) {
			((u32 *)cmd_data.init_data.rand)[i] ^= rand_get();
		}
		memcpy(root_page.header.cbc_iv, cmd_data.init_data.rand, AES_BLK_SIZE);
		memcpy(root_page.header.auth_rand, cmd_data.init_data.rand + AES_BLK_SIZE, AES_BLK_SIZE);
		stm_aes_encrypt(cmd_data.init_data.passwd, root_page.header.auth_rand, root_page.header.auth_rand_ct);
		stm_aes_encrypt(cmd_data.init_data.passwd, cmd_data.init_data.rand + AES_BLK_SIZE * 2, root_page.header.encrypt_key_ct);
		memcpy(root_page.header.salt, cmd_data.init_data.salt, AES_BLK_SIZE);
		memcpy(root_page.header.hashfn, cmd_data.init_data.hashfn, AES_BLK_SIZE);
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
		if (avail <= INITIALIZE_RAND_DATA_SIZE_WORDS) {
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
	if (waiting_for_button_press) {
		end_button_press_wait();
		finish_command_resp(BUTTON_PRESS_TIMEOUT);
	}
}

void cmd_packet_sent()
{
}

void long_button_press()
{
	if (waiting_for_long_button_press) {
		end_long_button_press_wait();
		switch(active_cmd) {
			break;
		}
	}
}

void button_press()
{
	if (waiting_for_button_press) {
		end_button_press_wait();
		switch(active_cmd) {
		case LOGIN:
			stm_aes_encrypt(cmd_data.login.password, root_page.header.auth_rand, cmd_data.login.cyphertext);
			if (memcmp(cmd_data.login.cyphertext, root_page.header.auth_rand_ct, AES_BLK_SIZE)) {
				finish_command_resp(BAD_PASSWORD);
			} else {
				finish_command_resp(OKAY);
				stm_aes_decrypt(cmd_data.login.password, root_page.header.encrypt_key_ct, encrypt_key);
				enter_state(LOGGED_IN);
			}
			break;
		case OPEN_ID:
			active_id = cmd_data.open_id.id;
			finish_command_resp(OKAY);
			break;
		case INITIALIZE: {
			cmd_data.init_data.started = 1;
			cmd_data.init_data.blocks_written = 0;
			cmd_data.init_data.random_data_gathered = 0;
			cmd_data.init_data.root_block_finalized = 0;
			flash_write_page(ID_BLK(MIN_ID), NULL, 0);
			finish_command_resp(OKAY);
			cmd_data.init_data.rand_avail_init = rand_avail();
			int p = INITIALIZE_RAND_DATA_SIZE_WORDS - cmd_data.init_data.rand_avail_init;
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
			stm_aes_encrypt(cmd_data.change_master_password.new_key, encrypt_key, root_page.header.encrypt_key_ct);
			stm_aes_encrypt(cmd_data.change_master_password.new_key, root_page.header.auth_rand, root_page.header.auth_rand_ct);
			memcpy(root_page.header.hashfn, cmd_data.change_master_password.hashfn, AES_BLK_SIZE);
			memcpy(root_page.header.salt, cmd_data.change_master_password.salt, AES_BLK_SIZE);
			flash_write_page(ID_BLK(0), (u8 *)&root_page, sizeof(root_page));
			break;
		case UPDATE_FIRMWARE:
			finish_command_resp(OKAY);
			enter_state(FIRMWARE_UPDATE);
			break;
		}
	} else if (!waiting_for_button_press && !waiting_for_long_button_press) {
		cmd_event_send(1, NULL, 0);
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

	memcpy(cmd_data.init_data.passwd, data, AES_BLK_SIZE);
	memcpy(cmd_data.init_data.hashfn, data + AES_BLK_SIZE, AES_BLK_SIZE);
	memcpy(cmd_data.init_data.salt, data + AES_BLK_SIZE * 2, AES_BLK_SIZE);
	memcpy(cmd_data.init_data.rand, data + AES_BLK_SIZE * 3, INIT_RAND_DATA_SZ);
	cmd_data.init_data.started = 0;

	data += INITIALIZE_CMD_SIZE;
	data_len -= INITIALIZE_CMD_SIZE;
	memset(cmd_data.init_data.userdata, 0, DEVICE_USERDATA_SIZE);
	memcpy(cmd_data.init_data.userdata, data, data_len);
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
	if (data_len != (AES_BLK_SIZE * 4)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	u8 *old_key = data;
	u8 *new_key = data + AES_BLK_SIZE;
	u8 *hashfn = data + AES_BLK_SIZE * 2;
	u8 *salt = data + AES_BLK_SIZE * 3;
	stm_aes_encrypt(old_key, root_page.header.auth_rand, cmd_data.change_master_password.cyphertext);
	if (memcmp(cmd_data.change_master_password.cyphertext, root_page.header.auth_rand_ct, AES_BLK_SIZE)) {
		finish_command_resp(BAD_PASSWORD);
		return;
	}
	stm_aes_decrypt(old_key, root_page.header.encrypt_key_ct, encrypt_key);
	memcpy(cmd_data.change_master_password.new_key, new_key, AES_BLK_SIZE);
	memcpy(cmd_data.change_master_password.hashfn, hashfn, AES_BLK_SIZE);
	memcpy(cmd_data.change_master_password.salt, salt, AES_BLK_SIZE);
	begin_button_press_wait();
}

void get_all_data_cmd(u8 *data, int data_len)
{
	dprint_s("GET_ALL_DATA\r\n");
	begin_long_button_press_wait();
}

void get_data_cmd(int id_cmd)
{
	if (!validate_present_id(id_cmd)) {
		dprint_s("GET_DATA ");
		dprint_dec(id_cmd);
		dprint_s("\r\n");
		u8 *addr =  ID_BLK(id_cmd);

		u16 sz = addr[2] + (addr[3] << 8);
		int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);
		derive_iv(id_cmd, cmd_data.get_data.iv);
		unsigned int k = 0;
		u8 *block = cmd_data.get_data.block;
		block[k] = addr[2]; k++;
		block[k] = addr[3]; k++;
		stm_aes_decrypt_cbc(encrypt_key, blk_count, cmd_data.get_data.iv, addr + SUB_BLK_SIZE, block + k);
		if (id_cmd != active_id) {
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
		finish_command(OKAY, block, k);
	}
}

void set_data_cmd(int id_cmd, u8 *data, int data_len)
{
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
		if (id_cmd == active_id) {
			u8 *addr =  ID_BLK(id_cmd);
			derive_iv(id_cmd, cmd_data.set_data.iv);
			memset(cmd_data.set_data.block, 0, AES_BLK_SIZE);
			cmd_data.set_data.block[2] = sz & 0xff;
			cmd_data.set_data.block[3] = sz >> 8;
			stm_aes_encrypt_cbc(encrypt_key, blk_count, cmd_data.set_data.iv, data, cmd_data.set_data.block + SUB_BLK_SIZE);
			flash_write_page(addr, cmd_data.set_data.block, (blk_count + 1) * SUB_BLK_SIZE);
		} else {
			finish_command_resp(ID_NOT_OPEN);
		}
	}
}

void delete_cmd(int id_cmd)
{
	if (!validate_present_id(id_cmd)) {
		dprint_s("DELETE_ID ");
		dprint_dec(id_cmd);
		dprint_s("\r\n");
		if (id_cmd == active_id) {
			flash_write_page(ID_BLK(id_cmd), NULL, 0);
		} else {
			finish_command_resp(ID_NOT_OPEN);
		}
	}
}

void cmd_connect()
{
	test_state = 0;
	stop_blinking();
	enter_state(RESET);
	finish_command_resp(OKAY);
}

void cmd_disconnect()
{
	//Cancel commands waiting for button press
	end_button_press_wait();
	end_long_button_press_wait();
	active_cmd = -1;
	active_id = -1;
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
		if (data_len != AES_BLK_SIZE) {
			finish_command_resp(INVALID_INPUT);
		} else {
			memcpy(cmd_data.login.password, data, AES_BLK_SIZE);
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
	case OPEN_ID:
		cmd_data.open_id.id = data[0];
		if (!validate_id(cmd_data.open_id.id)) {
			dprint_s("OPEN_ID ");
			dprint_dec(cmd_data.open_id.id);
			dprint_s("\r\n");
			active_id = -1;
			begin_button_press_wait();
		}
		break;
	case BACKUP_DEVICE:
		dprint_s("Backup device\r\n");
		begin_button_press_wait();
		break;
	case CLOSE_ID:
		active_id = -1;
		dprint_s("CLOSE_ID\r\n");
		finish_command_resp(OKAY);
		break;
	case GET_DATA:
		get_data_cmd(data[0]);
		break;
	case SET_DATA:
		set_data_cmd(data[0], data + 1, data_len - 1);
		break;
	case DELETE_ID: {
		delete_cmd(data[0]);
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
		active_id = -1;
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
		active_id = -1;
	}
	memcpy(&root_page, (u8 *)(&_root_page), BLK_SIZE);
	if (memcmp(root_page.header.signature, root_signature, AES_BLK_SIZE)) {
		dprint_s("STARTUP: uninitialized\r\n");
		enter_state(UNINITIALIZED);
	} else {
		dprint_s("STARTUP: logged out\r\n");
		enter_state(LOGGED_OUT);
	}
	u8 resp[1+(AES_BLK_SIZE*2)];
	resp[0] = device_state;
	memcpy(resp + 1, root_page.header.hashfn, AES_BLK_SIZE);
	memcpy(resp + 1 + AES_BLK_SIZE, root_page.header.salt, AES_BLK_SIZE);
	finish_command(OKAY, resp, sizeof(resp));
	return;
}

int disconnected_state(int cmd, u8 *data, int data_len)
{
	switch (cmd) {
	case CONNECT:
		cmd_connect();
		break;
	default:
		return -1;
	}
	return 0;
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
		ret = disconnected_state(active_cmd, data, data_len);
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
