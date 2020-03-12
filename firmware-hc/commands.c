#include <memory.h>
#include <stddef.h>

#include "commands.h"
#include "signetdev_common_priv.h"
#include "firmware_update_state.h"
#include "bootloader_state.h"
#include "types.h"
#include "stm32f7xx_hal.h"
#include "crc.h"

#include "usb_keyboard.h"

#include "print.h"
#include "flash.h"
#include "signet_aes.h"

#include "firmware_update_state.h"
#include "bootloader_state.h"

#include "rand.h"
#include "main.h"
#include "memory_layout.h"
#ifdef ENABLE_FIDO2
#include "ctaphid.h"
#endif

#include "usbd_hid.h"

//
// Globals
//

//Command code of the currently executing command or -1 if no command is executing
int active_cmd = -1;
static int cmd_iter_count = 0;
static int cmd_messages_remaining = 0;
static int g_write_db_tx_complete = 0;
static int g_read_db_tx_complete = 0;
static int g_uninitialized_wiped = 0;
static int g_mmc_tx_cplt = 0;
static int g_mmc_tx_dma_cplt = 0;
static int g_mmc_rx_cplt = 0;
int g_sync_root_block = 0;

enum device_state g_device_state = DS_DISCONNECTED;

static enum command_subsystem s_device_system_owner = NO_SUBSYSTEM;
static int s_ctap_subsystem_waiting = 0;
static int s_signet_subsystem_waiting = 0;
static int s_subsystem_release_requested = 1;

// Incoming buffer for next command request
u8 cmd_packet_buf[CMD_PACKET_BUF_SIZE] __attribute__((aligned(16)));

//Paramaters and temporary state for the command currently being
//executed
union cmd_data_u cmd_data;

u8 g_encrypt_key[AES_256_KEY_SIZE] __attribute__((aligned(16)));

u8 token_auth_rand_cyphertext[AES_256_KEY_SIZE];
u8 token_encrypt_key_cyphertext[AES_256_KEY_SIZE];

u8 g_root_block_version;
u8 g_db_version;

static union state_data_u state_data;

static void long_button_press_disconnected();
static void button_press_disconnected();
static void button_release_disconnected();

int g_root_page_valid = 0;
struct hc_device_data root_page;

extern struct hc_device_data _crypt_data1;
extern struct hc_device_data _crypt_data2;

struct hc_device_data *_root_page = NULL;

static int n_progress_components = 0;
int g_progress_level[8];
static int progress_maximum[8];
static int g_progress_check = 0;
static int g_progress_target_state = DS_DISCONNECTED;
static int waiting_for_button_press = 0;
static int waiting_for_long_button_press = 0;

//
// Read/write database blocks
//
extern MMC_HandleTypeDef hmmc1;

enum emmc_user g_emmc_user = EMMC_USER_NONE;

int g_emmc_user_ready[EMMC_NUM_USER];

enum db_action {
	DB_ACTION_NONE,
	DB_ACTION_READ,
	DB_ACTION_WRITE
};

static enum db_action g_db_action = DB_ACTION_NONE;

static int g_db_read_idx;
static u8 *g_db_read_dest;

static int g_db_write_idx;
static const u8 *g_db_write_src;

#include "usbd_msc_scsi.h"
#include "usbd_msc.h"

void emmc_user_storage_start();

static void read_block_complete();
static void write_block_complete();

void startup_cmd_iter();
static void write_block_complete();
void write_root_block(const u8 *data, int sz);

u32 compute_device_data_crc(struct hc_device_data *d);

void emmc_user_write_storage_tx_dma_complete(MMC_HandleTypeDef *hmmc);
void emmc_user_write_db_tx_dma_complete(MMC_HandleTypeDef *hmmc);

static void release_device(enum command_subsystem system);

extern MMC_HandleTypeDef hmmc1;

static void subsystem_idle_check()
{
	if (active_cmd == -1 && n_progress_components == 0) {
		release_device_request(SIGNET_SUBSYSTEM);
	}
}

void emmc_user_db_start()
{
	HAL_MMC_CardStateTypeDef cardState;
	switch (g_db_action) {
	case DB_ACTION_READ: {
		int idx = g_db_read_idx;
		u8 *dest = g_db_read_dest;
		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);
		HAL_MMC_ReadBlocks_DMA(&hmmc1,
		                       dest,
				       (idx - MIN_DATA_BLOCK + EMMC_DB_FIRST_BLOCK)*(HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ),
		                       BLK_SIZE/MSC_MEDIA_PACKET);
	}
	break;
	case DB_ACTION_WRITE: {
		int idx = g_db_write_idx;
		const u8 *src = g_db_write_src;
		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		HAL_MMC_WriteBlocks_DMA_Initial(&hmmc1,
		                                src,
		                                BLK_SIZE,
						(idx - MIN_DATA_BLOCK + EMMC_DB_FIRST_BLOCK)*(HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ),
						BLK_SIZE/MSC_MEDIA_PACKET);
	}
	break;
	default:
		break;
	}
}

int command_idle_ready()
{
	return g_read_db_tx_complete | g_write_db_tx_complete | g_mmc_tx_cplt | g_mmc_tx_dma_cplt | g_mmc_rx_cplt;
}

void command_idle()
{
	if (g_read_db_tx_complete) {
		g_read_db_tx_complete = 0;
		emmc_user_done();
		read_block_complete();
	}
	if (g_write_db_tx_complete) {
		g_write_db_tx_complete = 0;
		emmc_user_done();
		write_block_complete();
	}
	if (g_mmc_tx_cplt) {
		g_mmc_tx_cplt = 0;
		switch (g_emmc_user) {
		case EMMC_USER_STORAGE:
			emmc_user_write_storage_tx_complete(&hmmc1);
			break;
		case EMMC_USER_DB:
			g_write_db_tx_complete = 1;
			break;
		default:
			assert(0);
		}
	}
	if (g_mmc_tx_dma_cplt) {
		g_mmc_tx_dma_cplt = 0;
		switch (g_emmc_user) {
		case EMMC_USER_STORAGE:
			emmc_user_write_storage_tx_dma_complete(&hmmc1);
			break;
		case EMMC_USER_DB:
			emmc_user_write_db_tx_dma_complete(&hmmc1);
			break;
		default:
			assert(0);
		}
	}
	if (g_mmc_rx_cplt) {
		g_mmc_rx_cplt = 0;
		switch (g_emmc_user) {
		case EMMC_USER_STORAGE:
			emmc_user_read_storage_rx_complete();
			break;
		case EMMC_USER_DB:
			g_read_db_tx_complete = 1;
			break;
		default:
			assert(0);
		}
	}
}

void emmc_user_schedule()
{
	if (g_emmc_user == EMMC_USER_NONE) {
		if (g_emmc_user_ready[EMMC_USER_DB]) {
			g_emmc_user = EMMC_USER_DB;
			g_emmc_user_ready[EMMC_USER_DB] = 0;
			emmc_user_db_start();
		} else if (g_emmc_user_ready[EMMC_USER_STORAGE]) {
			g_emmc_user = EMMC_USER_STORAGE;
			g_emmc_user_ready[EMMC_USER_STORAGE] = 0;
			emmc_user_storage_start();
		}
	}
}

void emmc_user_done()
{
	g_emmc_user = EMMC_USER_NONE;
	emmc_user_schedule();
}

void emmc_user_queue(enum emmc_user user)
{
	assert_lit(!g_emmc_user_ready[user], 0, 1);
	g_emmc_user_ready[user] = 1;
}

void read_data_block (int idx, u8 *dest)
{
	if (idx == ROOT_DATA_BLOCK) {
		memcpy(dest, (u8 *)_root_page, BLK_SIZE);
		read_block_complete();
	} else {
		emmc_user_queue(EMMC_USER_DB);
		g_db_action = DB_ACTION_READ;
		g_db_read_idx = idx;
		g_db_read_dest = dest;
		emmc_user_schedule();
	}
}

void write_data_block (int idx, const u8 *src)
{
#ifdef BOOT_MODE_B
	invalidate_data_block_cache(idx);
#endif
	if (idx == ROOT_DATA_BLOCK) {
		write_root_block(src, BLK_SIZE);
	} else {
		emmc_user_queue(EMMC_USER_DB);
		g_db_action = DB_ACTION_WRITE;
		g_db_write_idx = idx;
		g_db_write_src = src;
		emmc_user_schedule();
	}
}

void HAL_MMC_RxCpltCallback(MMC_HandleTypeDef *hmmc1)
{
	g_mmc_rx_cplt = 1;
}

void emmc_user_write_db_tx_dma_complete(MMC_HandleTypeDef *hmmc)
{
	HAL_MMC_WriteBlocks_DMA_Cont(&hmmc1, NULL, 0);
}

void MMC_DMATXTransmitComplete(MMC_HandleTypeDef *hmmc)
{
	g_mmc_tx_dma_cplt = 1;
}

void HAL_MMC_ErrorCallback(MMC_HandleTypeDef *hmmc)
{
	UNUSED(hmmc);
	assert_lit(0, 1, 0);
}

void HAL_MMC_TxCpltCallback(MMC_HandleTypeDef *hmmc1)
{
	g_mmc_tx_cplt = 1;
}

//
// Misc functions
//

void sync_root_block()
{
	g_sync_root_block = 1;
}

int sync_root_block_pending()
{
	return g_sync_root_block;
}

void sync_root_block_immediate()
{
	write_root_block((const u8 *)&root_page, sizeof(root_page));
}

void write_root_block(const u8 *data, int sz)
{
	struct hc_device_data *d = (struct hc_device_data *)data;

	if (g_root_page_valid) {
		d->data_iteration = _root_page->data_iteration + 1;
	}
	d->crc = compute_device_data_crc(d);
	if (_root_page == &_crypt_data1) {
		_root_page = &_crypt_data2;
	} else if (_root_page == &_crypt_data2) {
		_root_page = &_crypt_data1;
	} else {
		_root_page = &_crypt_data1;
	}
	flash_write_page((u8 *)_root_page, data, sz);
}

void get_progress_check();
void delete_cmd_complete();
void get_data_cmd_complete();
void set_data_cmd_complete();
void initialize_cmd_complete();
void enter_progressing_state (enum device_state state, int _n_progress_components, int *_progress_maximum)
{
	g_device_state = state;
	usbd_scsi_device_state_change(g_device_state);
	g_progress_check = 0;
	n_progress_components = _n_progress_components;
	int i;
	for (i = 0; i < n_progress_components; i++) {
		g_progress_level[i] = 0;
		progress_maximum[i] = _progress_maximum[i];
	}
	get_progress_check();
	subsystem_idle_check();
}

void enter_state (enum device_state state)
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
		total += g_progress_level[i];
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

void get_progress_check ()
{
	if (active_cmd == GET_PROGRESS) {
		int total_progress = get_total_progress();
		int total_progress_maximum = get_total_progress_maximum();
		if (g_progress_target_state == g_device_state && total_progress > g_progress_check) {
			u8 resp[4*(8+1)] = {
				total_progress & 0xff,
				total_progress >> 8,
				               total_progress_maximum & 0xff,
				               total_progress_maximum >> 8,
			};
			int i;
			for (i = 0; i < n_progress_components; i++) {
				int j = i + 1;
				resp[j * 4] = g_progress_level[i] & 0xff;
				resp[j * 4 + 1] = g_progress_level[i] >> 8;
				resp[j * 4 + 2] = progress_maximum[i] & 0xff;
				resp[j * 4 + 3] = progress_maximum[i] >> 8;
			}
			finish_command(OKAY, resp, (n_progress_components + 1) * 4);
		} else if (g_progress_target_state != g_device_state) {
			finish_command_resp(INVALID_STATE);
		}
	}
}

u8 cmd_resp[CMD_PACKET_BUF_SIZE] __attribute__((aligned(16)));

void finish_command_multi (enum command_responses resp, int messages_remaining, const u8 *payload, int payload_len)
{
	int full_length = payload_len + CMD_PACKET_HEADER_SIZE;
	cmd_resp[0] = full_length & 0xff;
	cmd_resp[1] = (full_length >> 8) & 0xff;
	cmd_resp[2] = resp;
	cmd_resp[3] = messages_remaining & 0xff;
	cmd_resp[4] = (messages_remaining >> 8) & 0xff;
	cmd_resp[5] = g_device_state;
	if (payload) {
		memcpy(cmd_resp + CMD_PACKET_HEADER_SIZE, payload, payload_len);
	}
	if (!messages_remaining && !cmd_messages_remaining) {
		active_cmd = -1;
	}
	cmd_packet_send(cmd_resp, full_length);
	subsystem_idle_check();
}

void finish_command (enum command_responses resp, const u8 *payload, int payload_len)
{
	finish_command_multi(resp, 0, payload, payload_len);
}

void finish_command_resp (enum command_responses resp)
{
	finish_command(resp, NULL, 0);
}

void derive_iv(u32 id, u8 *iv)
{
	memset(iv, 0, AES_BLK_SIZE);
	switch (root_page.format) {
	case CURRENT_ROOT_BLOCK_FORMAT:
		//TODO: Handle DEVICE_ID_LEN > AES_BLK_SIZE
		memcpy(iv, root_page.device_id, DEVICE_ID_LEN);
		break;
	default:
		//TODO: What to do here?
		break;
	}
	((u32 *)(iv + AES_BLK_SIZE - 4))[0] += (u8)(id);
}

static void finalize_root_page_check()
{
	__disable_irq();
	if (rand_avail() >= (INIT_RAND_DATA_SZ/4) && !cmd_data.init_data.root_block_finalized && (cmd_data.init_data.blocks_written == NUM_DATA_BLOCKS)) {
		cmd_data.init_data.root_block_finalized = 1;
		__enable_irq();
		for (int i = 0; i < (INIT_RAND_DATA_SZ/4); i++) {
			((u32 *)cmd_data.init_data.rand)[i] ^= rand_get();
		}
		root_page.format = CURRENT_ROOT_BLOCK_FORMAT;
		root_page.db_format = CURRENT_DB_FORMAT;
		u8 *random = cmd_data.init_data.rand;

		u8 *device_id = random;
		random += DEVICE_ID_LEN;
		u8 *auth_rand_data = random;
		random += AUTH_RANDOM_DATA_LEN;
		u8 *keystore_key = random;
		random += AES_256_KEY_SIZE;

		memcpy(root_page.device_id, device_id, DEVICE_ID_LEN);
		memcpy(root_page.auth_random_cleartext, auth_rand_data, AUTH_RANDOM_DATA_LEN);
		signet_aes_256_encrypt_cbc(cmd_data.init_data.passwd, AUTH_RANDOM_DATA_LEN/AES_BLK_SIZE, NULL,
		                           auth_rand_data, root_page.profile_auth_data[0].auth_random_cyphertext);
		signet_aes_256_encrypt_cbc(cmd_data.init_data.passwd, HC_KEYSTORE_KEY_SIZE/AES_BLK_SIZE, NULL,
		                           keystore_key, root_page.profile_auth_data[0].keystore_key_cyphertext);
		memcpy(root_page.profile_auth_data[0].salt, cmd_data.init_data.salt, HC_HASH_FN_SALT_SZ);
		memcpy(root_page.profile_auth_data[0].hash_function_params, cmd_data.init_data.hashfn, HC_HASH_FUNCTION_PARAMS_LENGTH);
		sync_root_block();
	} else {
		__enable_irq();
	}
}

//
// System events
//

void get_rand_bits_cmd_check();
void login_cmd_iter();

#ifdef BOOT_MODE_B

void cmd_rand_update()
{
	int avail = rand_avail();
	switch (g_device_state) {
	case DS_INITIALIZING:
		if (avail <= (INIT_RAND_DATA_SZ/4)) {
			cmd_data.init_data.random_data_gathered = avail - cmd_data.init_data.rand_avail_init;
			g_progress_level[1] = cmd_data.init_data.random_data_gathered;
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

#endif

extern int block_read_cache_updating;

static void read_block_complete()
{
#ifdef BOOT_MODE_B
	if (db3_read_block_complete())
		return;
#endif
	switch (active_cmd) {
	case READ_BLOCK:
		finish_command(OKAY, cmd_data.read_block.block, BLK_SIZE);
		return;
	default:
		break;
	}
}

static void initializing_iter()
{
	cmd_data.init_data.blocks_written++;
	g_progress_level[0] = cmd_data.init_data.blocks_written;
	if (g_progress_level[0] > progress_maximum[0]) g_progress_level[0] = progress_maximum[0];
	g_progress_level[1] = cmd_data.init_data.random_data_gathered;
	get_progress_check();

	if (cmd_data.init_data.blocks_written < NUM_DATA_BLOCKS) {
		struct block *blk = db3_initialize_block(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK, (struct block *)cmd_data.init_data.block);
		write_data_block(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK, (u8 *)blk);
	} else if (cmd_data.init_data.blocks_written == NUM_DATA_BLOCKS) {
		finalize_root_page_check();
	} else {
		g_root_block_version = CURRENT_ROOT_BLOCK_FORMAT;
		g_db_version = CURRENT_DB_FORMAT;
		g_root_page_valid = 1;
		db3_startup_scan(cmd_data.init_data.block, &cmd_data.init_data.blk_info);
	}
}

static void write_block_complete()
{
#ifdef BOOT_MODE_B
	if (g_sync_root_block) {
		g_sync_root_block = 0;
		if (s_subsystem_release_requested) {
			s_subsystem_release_requested = 0;
			release_device(s_device_system_owner);
		}
	}
	if (db3_write_block_complete())
		return;
	switch (g_device_state) {
	case DS_INITIALIZING:
		initializing_iter();
		break;
	case DS_WIPING:
		cmd_data.wipe_data.block_idx++;
		g_progress_level[0] = cmd_data.wipe_data.block_idx;
		get_progress_check();
		if (cmd_data.wipe_data.block_idx == NUM_STORAGE_BLOCKS) {
			g_uninitialized_wiped = 1;
			enter_state(DS_UNINITIALIZED);
		} else {
			memset(cmd_data.wipe_data.block, 0, BLK_SIZE);
			write_data_block(cmd_data.wipe_data.block_idx, cmd_data.wipe_data.block);
		}
		break;
	default:
		break;
	}
#endif
	switch (active_cmd) {
	case WRITE_CLEARTEXT_PASSWORD:
	case CHANGE_MASTER_PASSWORD:
	case WRITE_BLOCK:
	case ERASE_BLOCK:
		finish_command_resp(OKAY);
		break;
	case WRITE_FLASH:
		write_flash_cmd_complete();
		break;
	case STARTUP:
		startup_cmd_iter();
		break;
	default:
		break;
	}
}

void flash_write_complete()
{
	write_block_complete();
	firmware_update_write_block_complete();
}


void flash_write_failed()
{
	if (active_cmd != -1) {
		finish_command_resp(WRITE_FAILED);
	}
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
#ifdef BOOT_MODE_B
	switch(active_cmd) {
	case READ_ALL_UIDS:
		read_all_uids_cmd_complete();
		break;
	}
#endif
}

void long_button_press()
{
	if (waiting_for_long_button_press) {
		end_long_button_press_wait();
		switch(active_cmd) {
#ifdef BOOT_MODE_B
		case READ_ALL_UIDS:
			read_all_uids_cmd_iter();
			break;
		case UPDATE_UIDS:
			update_uid_cmd_complete();
			break;
#endif
		case UPDATE_FIRMWARE:
			update_firmware_cmd_complete();
			break;
#ifdef BOOT_MODE_B
		case WIPE: {
			finish_command_resp(OKAY);
			cmd_data.wipe_data.block_idx = ROOT_DATA_BLOCK;
			memset(cmd_data.wipe_data.block, 0, BLK_SIZE);
			write_data_block(cmd_data.wipe_data.block_idx,
					cmd_data.wipe_data.block);
			int temp[] = {NUM_STORAGE_BLOCKS};
			enter_progressing_state(DS_WIPING, 1, temp);
		}
		break;
		case BACKUP_DEVICE:
			finish_command_resp(OKAY);
			state_data.backup.prev_state = g_device_state;
			enter_state(DS_BACKING_UP_DEVICE);
			break;
		case RESTORE_DEVICE:
			finish_command_resp(OKAY);
			enter_state(DS_RESTORING_DEVICE);
			break;
		case INITIALIZE:
			initialize_cmd_complete();
			break;
		case CHANGE_MASTER_PASSWORD:
			switch (root_page.format) {
			case CURRENT_ROOT_BLOCK_FORMAT:
				signet_aes_256_encrypt_cbc(cmd_data.change_master_password.new_key, AUTH_RANDOM_DATA_LEN/AES_BLK_SIZE, NULL,
				                           root_page.auth_random_cleartext,
							   root_page.profile_auth_data[0].auth_random_cyphertext);
				signet_aes_256_encrypt_cbc(cmd_data.change_master_password.new_key, HC_KEYSTORE_KEY_SIZE/AES_BLK_SIZE, NULL,
				                           cmd_data.change_master_password.keystore_key,
							   root_page.profile_auth_data[0].keystore_key_cyphertext);
				memcpy(root_page.profile_auth_data[0].salt, cmd_data.change_master_password.salt, HC_HASH_FN_SALT_SZ);
				memcpy(root_page.profile_auth_data[0].hash_function_params, cmd_data.change_master_password.hashfn, HC_HASH_FUNCTION_PARAMS_LENGTH);
				break;
			default:
				finish_command_resp(UNKNOWN_DB_FORMAT);
				return;
			}
			sync_root_block();
			break;
#endif
		}
	} else if (g_device_state == DS_DISCONNECTED) {
		long_button_press_disconnected();
	}
}

void button_release()
{
	if (waiting_for_long_button_press) {
		resume_blinking();
	} else if (g_device_state == DS_DISCONNECTED) {
		//HC_TODO: Not supported yet
		//button_release_disconnected();
	}
}

int attempt_login_256 (const u8 *auth_key, const u8 *auth_rand, const u8 *auth_rand_cyphertext, const u8 *encrypt_key_cyphertext, u8 *encrypt_key)
{
	u8 auth_rand_cyphertext_test[AUTH_RANDOM_DATA_LEN];
	signet_aes_256_encrypt_cbc(auth_key, AUTH_RANDOM_DATA_LEN/AES_BLK_SIZE, NULL, auth_rand, auth_rand_cyphertext_test);
	if (memcmp(auth_rand_cyphertext_test, auth_rand_cyphertext, AUTH_RANDOM_DATA_LEN)) {
		return 0;
	} else {
		signet_aes_256_decrypt_cbc(auth_key, HC_KEYSTORE_KEY_SIZE/AES_BLK_SIZE, NULL, encrypt_key_cyphertext, encrypt_key);
		return 1;
	}
}

void login_cmd_iter ()
{
	if (cmd_data.login.authenticated) {
		if (rand_avail() >= (AES_256_KEY_SIZE/4)) {
			for (int i = 0; i < (AES_256_KEY_SIZE/4); i++) {
				cmd_data.login.token[i] = rand_get();
			}
			signet_aes_256_encrypt_cbc((u8 *)cmd_data.login.token, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
			                           root_page.auth_random_cleartext, token_auth_rand_cyphertext);
			signet_aes_256_encrypt_cbc((u8 *)cmd_data.login.token, AES_256_KEY_SIZE/AES_BLK_SIZE, NULL,
			                           g_encrypt_key, token_encrypt_key_cyphertext);
			finish_command(OKAY, (u8 *)cmd_data.login.gen_token, AES_256_KEY_SIZE);
			enter_state(DS_LOGGED_IN);
		}
	}
}

static int cleartext_pass_index = -1;
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
		generate_backspaces(cleartext_type_buf, index + 1);
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
		cleartext_type_buf[0] = 0;
		cleartext_type_buf[1] = 30 + cleartext_pass_index;
		usb_keyboard_type(cleartext_type_buf, 2);
		timer_start(2000);
	}
}

static void long_button_press_disconnected()
{
#ifndef SIGNET_HC
	switch (root_page.signature[0]) {
	case 2: {
		int index = cleartext_pass_index;
		struct cleartext_pass *p = root_page.header.v2.cleartext_passwords + index;
		if (p->format != 0xff && p->format && (p->scancode_entries*2) <= 126) {
			cleartext_pass_index = -1;
			generate_backspaces(cleartext_type_buf, index+1);
			memcpy(cleartext_type_buf + (index+1)*4, p->scancodes, p->scancode_entries*2);
			cleartext_pass_typing = 1;
			usb_keyboard_type(cleartext_type_buf, p->scancode_entries + (index + 1)*2);
		} else {
			timer_timeout();
			timer_stop();
		}
	}
	break;
	}
#endif
}

void button_press_unprompted()
{
	switch (g_device_state) {
	case DS_DISCONNECTED:
		//HC_TODO: Not supported yet
		//button_press_disconnected();
		break;
	default:
		cmd_event_send(1, NULL, 0);
		break;
	}
}

void button_press()
{
	if (waiting_for_button_press) {
		end_button_press_wait();
		switch(active_cmd) {
#ifndef SIGNET_HC
		case READ_CLEARTEXT_PASSWORD: {
			int idx = cmd_data.read_cleartext_password.idx;
			struct cleartext_pass *p = root_page.header.v2.cleartext_passwords;
			finish_command(OKAY, (u8 *)(p + idx), CLEARTEXT_PASS_SIZE);
		}
		break;
		case WRITE_CLEARTEXT_PASSWORD: {
			int idx = cmd_data.write_cleartext_password.idx;
			struct cleartext_pass *p = root_page.header.v2.cleartext_passwords;
			memcpy(p + idx, cmd_data.write_cleartext_password.data, CLEARTEXT_PASS_SIZE);
			flash_write_page(ID_BLK(0), (u8 *)&root_page, sizeof(root_page));
		}
		break;
#endif
#ifdef BOOT_MODE_B
		case LOGIN:
			switch (root_page.format) {
			case CURRENT_ROOT_BLOCK_FORMAT: {
				int rc = attempt_login_256(cmd_data.login.password,
				                           root_page.auth_random_cleartext,
				                           root_page.profile_auth_data[0].auth_random_cyphertext,
				                           root_page.profile_auth_data[0].keystore_key_cyphertext,
				                           g_encrypt_key);
				if (rc) {
					if (cmd_data.login.gen_token) {
						cmd_data.login.authenticated = 1;
						login_cmd_iter();
					} else {
						finish_command_resp(OKAY);
						enter_state(DS_LOGGED_IN);
					}
				} else {
					finish_command_resp(BAD_PASSWORD);
				}
			}
			break;
			default:
				finish_command_resp(UNKNOWN_DB_FORMAT);
				break;
			}
			break;
		case LOGIN_TOKEN: {
			int rc;
			switch (root_page.format) {
			case CURRENT_ROOT_BLOCK_FORMAT:
				rc = attempt_login_256((u8 *)cmd_data.login_token.token,
				                       root_page.auth_random_cleartext,
				                       token_auth_rand_cyphertext,
				                       token_encrypt_key_cyphertext,
				                       g_encrypt_key);
				if (rc) {
					finish_command_resp(OKAY);
					enter_state(DS_LOGGED_IN);
				} else {
					finish_command_resp(BAD_PASSWORD);
				}
				break;
			}
		}
		break;
		case UPDATE_UID:
			update_uid_cmd_complete();
			break;
		case READ_UID:
			read_uid_cmd_complete();
			break;
#endif
		case BUTTON_WAIT:
			finish_command_resp(OKAY);
			break;
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
	if (g_device_state == DS_DISCONNECTED) {
		cleartext_pass_typing = 0;
	}
}

//
// Command functions. These are called in response to incoming command packets
//

extern int test_state;

void initialize_cmd(u8 *data, int data_len)
{
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
	begin_long_button_press_wait();
}

void initialize_cmd_complete()
{
	cmd_data.init_data.started = 1;
	cmd_data.init_data.blocks_written = 0;
	cmd_data.init_data.random_data_gathered = 0;
	cmd_data.init_data.root_block_finalized = 0;

	struct block *blk = db3_initialize_block(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK, (struct block *)cmd_data.init_data.block);
	write_data_block(cmd_data.init_data.blocks_written + MIN_DATA_BLOCK, (u8 *)blk);
	finish_command_resp(OKAY);
	cmd_data.init_data.rand_avail_init = rand_avail();
	int p = (INIT_RAND_DATA_SZ/4) - cmd_data.init_data.rand_avail_init;
	if (p < 0) p = 0;
	int temp[] = {NUM_DATA_BLOCKS, p, 1};
	enter_progressing_state(DS_INITIALIZING, 3, temp);
}

void wipe_cmd()
{
	begin_long_button_press_wait();
}

void get_progress_cmd(u8 *data, int data_len)
{
	g_progress_check = data[0] | (data[1] << 8);
	data += 2;
	g_progress_target_state = data[0] | (data[1] << 8);
	data += 2;
	if (g_progress_target_state != g_device_state) {
		finish_command_resp(INVALID_STATE);
	} else {
		get_progress_check();
	}
}

void backup_device_cmd(u8 *data, int data_len)
{
	begin_button_press_wait();
}

void restore_device_cmd ()
{
	begin_long_button_press_wait();
}

void read_block_cmd (u8 *data, int data_len)
{
	if (data_len != 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	int idx = *data;
	if (idx >= NUM_STORAGE_BLOCKS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	cmd_data.read_block.block_idx = idx;
	read_data_block(cmd_data.read_block.block_idx, cmd_data.read_block.block);
}

void write_block_cmd(u8 *data, int data_len)
{
	//TODO: consider making the header 2 or 4 bytes to we can have alignment for DMA transfer
	if (data_len != (1 + BLK_SIZE)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	cmd_data.write_block.block_idx = *data;
	data++;
	memcpy(cmd_data.write_block.block, data, BLK_SIZE);
	if (cmd_data.write_block.block_idx >= NUM_STORAGE_BLOCKS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	write_data_block(cmd_data.write_block.block_idx, cmd_data.write_block.block);
}

void erase_block_cmd(u8 *data, int data_len)
{
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
	memset(cmd_data.erase_block.block, 0, BLK_SIZE);
	write_data_block(idx, cmd_data.erase_block.block);
}

void get_device_capacity_cmd(u8 *data, int data_len)
{
	//TODO
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
	if (data_len != ((AES_256_KEY_SIZE * 2) + HASH_FN_SZ + SALT_SZ_V2)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	u8 *old_key = data;
	u8 *new_key = data + AES_256_KEY_SIZE;
	u8 *hashfn = data + (AES_256_KEY_SIZE * 2);
	u8 *salt = data + (AES_256_KEY_SIZE * 2) + HASH_FN_SZ;
	memcpy(cmd_data.change_master_password.hashfn, hashfn,HC_HASH_FUNCTION_PARAMS_LENGTH);
	memcpy(cmd_data.change_master_password.salt, salt, HC_HASH_FN_SALT_SZ);
	memcpy(cmd_data.change_master_password.new_key, new_key, HC_KEYSTORE_KEY_SIZE);
	switch (root_page.format) {
	case CURRENT_ROOT_BLOCK_FORMAT: {
		int rc = attempt_login_256(old_key,
					   root_page.auth_random_cleartext,
					   root_page.profile_auth_data[0].auth_random_cyphertext,
					   root_page.profile_auth_data[0].keystore_key_cyphertext,
					   cmd_data.change_master_password.keystore_key);
		if (!rc) {
			finish_command_resp(BAD_PASSWORD);
			return;
		}
	} break;
	default:
		finish_command_resp(UNKNOWN_DB_FORMAT);
		return;
	}
	begin_long_button_press_wait();
}

void cmd_disconnect()
{
	//Cancel commands waiting for button press
	end_button_press_wait();
	end_long_button_press_wait();
	active_cmd = -1;
	enter_state(DS_DISCONNECTED);
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
	int sz = data[0] + (data[1] << 8);
	cmd_data.get_rand_bits.sz = sz;
	if (cmd_data.get_rand_bits.sz >= BLK_SIZE) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	memset(cmd_data.get_rand_bits.block, 0, (sz + 3)/4);
	get_rand_bits_cmd_check();
}

void login_cmd (u8 *data, int data_len)
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

#ifndef SIGNET_HC
void enter_mobile_mode_cmd()
{
	cmd_disconnect();
	usb_set_mobile_mode();
	usb_reconnect_device();
}
#endif

int is_device_wiped()
{
	//NEN_TODO: Is checking if the root blocked is wiped sufficient?
	u32 *data_area = (u32 *)(_root_page);
	for (int i = 0; i < (BLK_SIZE)/4; i++) {
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
	case WIPE:
		wipe_cmd();
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
#ifndef SIGNET_HC
	case ENTER_MOBILE_MODE:
		enter_mobile_mode_cmd();
		break;
#endif
#ifdef FACTORY_MODE
	case UPDATE_FIRMWARE:
		update_firmware_cmd(data, data_len);
		update_firmware_cmd_complete();
		break;
#else
	case UPDATE_FIRMWARE:
		if (g_uninitialized_wiped) {
			update_firmware_cmd(data, data_len);
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
#ifndef SIGNET_HC
void write_cleartext_password(u8 *data, int data_len);
void read_cleartext_password(u8 *data, int data_len);
void read_cleartext_password_names();
#endif

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
		begin_long_button_press_wait();
		break;
	case RESTORE_DEVICE:
		restore_device_cmd();
		break;
	case LOGIN:
		login_cmd(data, data_len);
		break;
	case LOGIN_TOKEN:
		login_token_cmd(data, data_len);
		break;
	case GET_DEVICE_CAPACITY:
		get_device_capacity_cmd(data, data_len);
		break;
#if NEN_TODO
	case ENTER_MOBILE_MODE:
		enter_mobile_mode_cmd();
		break;
#endif
#ifdef FACTORY_MODE
	case UPDATE_FIRMWARE:
		update_firmware_cmd(data, data_len);
		update_firmware_cmd_complete();
		break;
#endif
	default:
		return -1;
	}
	return 0;
}

#ifndef SIGNET_HC
void write_cleartext_password(u8 *data, int data_len)
{
	if (data_len != (CLEARTEXT_PASS_SIZE + 1)) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	u8 idx = data[0];
	data++;
	data_len--;
	if (idx > NUM_CLEARTEXT_PASS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	cmd_data.write_cleartext_password.idx = idx;
	memcpy(cmd_data.write_cleartext_password.data, data, data_len);
	begin_button_press_wait();
}

void read_cleartext_password(u8 *data, int data_len)
{
	if (data_len != 1) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	u8 idx = data[0];
	data++;
	data_len--;
	if (idx > NUM_CLEARTEXT_PASS) {
		finish_command_resp(INVALID_INPUT);
		return;
	}
	cmd_data.read_cleartext_password.idx = idx;
	begin_button_press_wait();
}

void read_cleartext_password_names()
{
	u8 *block = cmd_data.read_cleartext_password_names.block;
	struct cleartext_pass *p = root_page.header.v2.cleartext_passwords;
	int i;
	int j = 0;
	for (i = 0; i < NUM_CLEARTEXT_PASS; i++) {
		block[j] = p->format;
		if (p->format == 0 || p->format == 0xff) {
			memset(block + j + 1, 0, CLEARTEXT_PASS_NAME_SIZE);
		} else {
			memcpy(block + j + 1, p->name_utf8, CLEARTEXT_PASS_NAME_SIZE);
		}
		j += (CLEARTEXT_PASS_NAME_SIZE + 1);
		p++;
	}
	finish_command(OKAY, (u8 *)block, NUM_CLEARTEXT_PASS * (CLEARTEXT_PASS_NAME_SIZE + 1));
}
#endif

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
	}
	break;
	case UPDATE_UIDS:
	case UPDATE_UID: {
		//TODO: move this all into db.c
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
		if (active_cmd == UPDATE_UID) {
			update_uid_cmd(uid, data, data_len, sz, 1 /* short press */);
		} else {
			if (!cmd_iter_count) {
				update_uid_cmd(uid, data, data_len, sz, 2 /* long press */);
			} else {
				update_uid_cmd(uid, data, data_len, sz, 0 /* no press */);
			}
		}
	}
	break;
	case READ_ALL_UIDS: {
		if (data_len < 1) {
			finish_command_resp(INVALID_INPUT);
			return 0;
		}
		int masked = data[0];
		read_all_uids_cmd(masked);
	}
	break;
	case TYPE: {
		int n_chars = data_len >> 1;
		if (n_chars * 4 > sizeof(cmd_data.type_data.chars)) {
			finish_command_resp(INVALID_INPUT);
			break;
		}
		for (int i = 0; i < n_chars; i++) {
			(cmd_data.type_data.chars + i * 4)[0] = data[i*2 + 0];
			(cmd_data.type_data.chars + i * 4)[1] = data[i*2 + 1];
			(cmd_data.type_data.chars + i * 4)[2] = 0;
			(cmd_data.type_data.chars + i * 4)[3] = 0;
		}
		usb_keyboard_type(cmd_data.type_data.chars, n_chars);
	}
	break;
	case BUTTON_WAIT:
		begin_button_press_wait();
		break;
	case LOGOUT:
		enter_state(DS_LOGGED_OUT);
		finish_command_resp(OKAY);
		break;
	case UPDATE_FIRMWARE:
		update_firmware_cmd(data, data_len);
		begin_long_button_press_wait();
		break;
#ifndef SIGNET_HC
	case READ_CLEARTEXT_PASSWORD_NAMES:
		read_cleartext_password_names();
		break;
	case READ_CLEARTEXT_PASSWORD:
		read_cleartext_password(data, data_len);
		break;
	case WRITE_CLEARTEXT_PASSWORD:
		write_cleartext_password(data, data_len);
		break;
#endif
	default:
		return -1;
	}
	return 0;
}

int backing_up_device_state (int cmd, u8 *data, int data_len)
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
		enter_state(DS_DISCONNECTED);
		finish_command_resp(OKAY);
		break;
	default:
		return -1;
	}
	return 0;
}

void startup_cmd_iter()
{
	u8 *resp = cmd_data.startup.resp;
	memset(cmd_data.startup.resp, 0, sizeof(cmd_data.startup.resp));
	resp[0] = SIGNET_HC_MAJOR_VERSION;
	resp[1] = SIGNET_HC_MINOR_VERSION;
	resp[2] = SIGNET_HC_STEP_VERSION;
	resp[3] = g_device_state;
	resp[4] = 0;
	resp[5] = 0;
	resp[6] = (u8)flash_get_boot_mode();
	resp[7] = 0;
#ifdef BOOT_MODE_B
	if (!g_root_page_valid) {
		g_uninitialized_wiped = 0;
		enter_state(DS_UNINITIALIZED);
		resp[3] = g_device_state;
		finish_command(OKAY, cmd_data.startup.resp, sizeof(cmd_data.startup.resp));
		return;
	}
	g_root_block_version = root_page.format;
	switch (root_page.format) {
	case CURRENT_ROOT_BLOCK_FORMAT:
		memcpy(resp + STARTUP_RESP_INFO_SIZE, root_page.profile_auth_data[0].hash_function_params, HC_HASH_FUNCTION_PARAMS_LENGTH);
		memcpy(resp + STARTUP_RESP_INFO_SIZE + HC_HASH_FUNCTION_PARAMS_LENGTH, root_page.profile_auth_data[0].salt, HC_HASH_FN_SALT_SZ);
		g_db_version = root_page.db_format;
		resp[4] = g_root_block_version;
		resp[5] = g_db_version;
		resp[7] = root_page.upgrade_state;
		break;
	default:
		g_uninitialized_wiped = 0;
		enter_state(DS_UNINITIALIZED);
		resp[3] = g_device_state;
		finish_command(UNKNOWN_DB_FORMAT, cmd_data.startup.resp, sizeof(cmd_data.startup.resp));
		return;
	}

	switch (g_db_version) {
	case CURRENT_DB_FORMAT:
		db3_startup_scan(cmd_data.startup.block, &cmd_data.startup.blk_info);
		break;
	default:
		g_uninitialized_wiped = 0;
		enter_state(DS_UNINITIALIZED);
		resp[3] = g_device_state;
		finish_command(UNKNOWN_DB_FORMAT, cmd_data.startup.resp, sizeof(cmd_data.startup.resp));
		return;
	}
#else
	enter_state(DS_BOOTLOADER);
	resp[3] = g_device_state;
	finish_command(OKAY, cmd_data.startup.resp, sizeof(cmd_data.startup.resp));
#endif
}

u32 compute_device_data_crc(struct hc_device_data *d)
{
	return crc_32(((u8 *)d) + 4, sizeof(struct hc_device_data) - 4);
}

void cmd_init()
{
	u32 crc1 = compute_device_data_crc(&_crypt_data1);
	u32 crc2 = compute_device_data_crc(&_crypt_data2);
	_root_page = NULL;
	if (crc1 == _crypt_data1.crc) {
		if (crc2 == _crypt_data2.crc) {
			if (_crypt_data1.data_iteration > _crypt_data2.data_iteration) {
				_root_page = &_crypt_data1;
			} else {
				_root_page = &_crypt_data2;
			}
		} else {
			_root_page = &_crypt_data1;
		}
	} else {
		if (crc2 == _crypt_data2.crc) {
			_root_page = &_crypt_data2;
		} else {
			_root_page = NULL;
		}
	}
	if (_root_page) {
		memcpy(&root_page, (u8 *)_root_page, sizeof(root_page));
		g_root_page_valid = 1;
	} else {
		g_root_page_valid = 0;
	}
}

void startup_cmd (u8 *data, int data_len)
{
	if (g_device_state != DS_DISCONNECTED) {
		stop_blinking();
		end_button_press_wait();
		end_long_button_press_wait();
	}
	cmd_init();
	startup_cmd_iter();
}

int request_device(enum command_subsystem system)
{
	__disable_irq();
	if (s_device_system_owner == system) {
		__enable_irq();
		return 1;
	}
	if (s_device_system_owner == NO_SUBSYSTEM) {
		s_device_system_owner = system;
		__enable_irq();
		return 1;
	}
	switch (system) {
	case SIGNET_SUBSYSTEM:
		s_signet_subsystem_waiting = 1;
		break;
	case CTAP_SUBSYSTEM:
		s_ctap_subsystem_waiting = 1;
		break;
	default:
		break;
	}
	__enable_irq();
	return 0;
}

static int restart_signet_command();

enum command_subsystem device_subsystem_owner()
{
	return s_device_system_owner;
}

int release_device_request(enum command_subsystem system)
{
	__disable_irq();
	//
	//Do nothing if we don't own the device or the root block
	//needs updating
	//
	if (system != s_device_system_owner) {
		__enable_irq();
		return 0;
	}
	if (g_sync_root_block) {
		//Wait until the root block is synchronized to release the device
		s_subsystem_release_requested = 1;
		__enable_irq();
		return 0;
	}
	release_device(system);
	return 1;
}

static void release_device(enum command_subsystem system)
{
	__disable_irq();
#ifdef ENABLE_FIDO2
	if (s_ctap_subsystem_waiting && system != CTAP_SUBSYSTEM) {
		s_ctap_subsystem_waiting = 0;
		s_device_system_owner = CTAP_SUBSYSTEM;
		__enable_irq();
		ctaphid_idle();
	} else
#endif
	if (s_signet_subsystem_waiting && system != SIGNET_SUBSYSTEM) {
		s_signet_subsystem_waiting = 0;
		s_device_system_owner = SIGNET_SUBSYSTEM;
		__enable_irq();
		if (!restart_signet_command()) {
			USBD_HID_rx_resume(INTERFACE_CMD);
		}
	} else {
		s_device_system_owner = NO_SUBSYSTEM;
		__enable_irq();
	}
	return;
}

void cmd_packet_recv()
{
	u8 *data = cmd_packet_buf;
	int data_len = data[0] + (data[1] << 8) - CMD_PACKET_HEADER_SIZE;
	int next_active_cmd = data[2];
	int messages_remaining = data[3] + (data[4] << 8);
	int prev_active_cmd = active_cmd;
	data += CMD_PACKET_HEADER_SIZE;

	int waiting_for_a_button_press = waiting_for_button_press | waiting_for_long_button_press;

	if (next_active_cmd == DISCONNECT) {
		cmd_disconnect();
		USBD_HID_rx_resume(INTERFACE_CMD);
		return;
	}

	if (prev_active_cmd != -1 && next_active_cmd == CANCEL_BUTTON_PRESS && !waiting_for_a_button_press) {
		//Ignore button cancel requests with no button press waiting
		USBD_HID_rx_resume(INTERFACE_CMD);
		return;
	}

	if (prev_active_cmd != -1 && waiting_for_a_button_press && next_active_cmd == CANCEL_BUTTON_PRESS) {
		end_button_press_wait();
		finish_command_resp(BUTTON_PRESS_CANCELED);
		USBD_HID_rx_resume(INTERFACE_CMD);
		return;
	}
	if (active_cmd != next_active_cmd) {
		cmd_iter_count = 0;
	}
	active_cmd = next_active_cmd;
	if (restart_signet_command() == 0) {
		USBD_HID_rx_resume(INTERFACE_CMD);
	}
}

static int restart_signet_command()
{
	u8 *data = cmd_packet_buf;
	int data_len = data[0] + (data[1] << 8) - CMD_PACKET_HEADER_SIZE;
	int messages_remaining = data[3] + (data[4] << 8);
	data += CMD_PACKET_HEADER_SIZE;
	if (!request_device(SIGNET_SUBSYSTEM)) {
		return 1;
	}
	cmd_messages_remaining = messages_remaining;

	if (active_cmd == STARTUP) {
		startup_cmd(data, data_len);
		return 0;
	}

	//Always allow the GET_DEVICE_STATE command. It's easiest to handle it here
	if (active_cmd == GET_DEVICE_STATE) {
		u8 resp[] = {g_device_state};
		finish_command(OKAY, resp, sizeof(resp));
		return 0;
	}
	int ret = -1;

	if (active_cmd == CANCEL_BUTTON_PRESS) {
		//If we didn't handle button cancelation press above, it could
		//be a sign of a bug
		active_cmd = -1;
		subsystem_idle_check();
		return 0;
	}

	//Every command should be able to accept CMD_PACKET_PAYLOAD_SIZE bytes of
	//data. If there is more, we reject it here.
	if (data_len > CMD_PACKET_PAYLOAD_SIZE) {
		finish_command_resp(INVALID_INPUT);
		return 0;
	}

	switch (g_device_state) {
	case DS_DISCONNECTED:
		break;
#ifdef BOOT_MODE_B
	case DS_UNINITIALIZED:
		ret = uninitialized_state(active_cmd, data, data_len);
		break;
	case DS_INITIALIZING:
		ret = initializing_state(active_cmd, data, data_len);
		break;
	case DS_WIPING:
		ret = wiping_state(active_cmd, data, data_len);
		break;
	case DS_BACKING_UP_DEVICE:
		ret = backing_up_device_state(active_cmd, data, data_len);
		break;
	case DS_RESTORING_DEVICE:
		ret = restoring_device_state(active_cmd, data, data_len);
		break;
#endif
	case DS_FIRMWARE_UPDATE:
		ret = firmware_update_state(active_cmd, data, data_len);
		break;
	case DS_ERASING_PAGES:
		ret = erasing_pages_state(active_cmd, data, data_len);
		break;
	case DS_BOOTLOADER:
		ret = bootloader_state(active_cmd, data, data_len);
		break;
#ifdef BOOT_MODE_B
	case DS_LOGGED_OUT:
		ret = logged_out_state(active_cmd, data, data_len);
		break;
	case DS_LOGGED_IN:
		ret = logged_in_state(active_cmd, data, data_len);
		break;
#endif
	default:
		break;
	}
	if (ret) {
		finish_command_resp(INVALID_STATE);
	}
	if (messages_remaining) {
		cmd_iter_count++;
	}
#ifdef TESTING_MODE
	if (waiting_for_button_press) {
		button_press();
	}
	if (waiting_for_long_button_press) {
		long_button_press();
	}
#endif
	return 0;
}
