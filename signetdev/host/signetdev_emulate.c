
#include "signetdev/common/signetdev_common.h"
#include "signetdev/host/signetdev_priv.h"

#include <stdio.h>
#include <gcrypt.h>
#include <memory.h>

struct db_uid_mapping {
	int block;
	int index;
};

static struct {
	enum device_state state;
	FILE *db_file;
	u8 device_data[NUM_STORAGE_BLOCKS][BLK_SIZE];
	u8 encrypt_key[AES_256_KEY_SIZE];
	int header_version;
	int db_version;
	gcry_cipher_hd_t aes256_ecb;
	gcry_cipher_hd_t aes256_cbc;
	gcry_md_hd_t crc32;
	int n_progress_components;
	int progress_levels[8];
	int progress_maximums[8];
	int progress_level;
	int progress_maximum;
	int progress_check;
	enum device_state progress_state;
	struct send_message_req *msg;

	struct db_uid_mapping uid_map[MAX_UID + 1];

	struct {
		int uid;
		int masked;
		int expected_remaining;
	} read_all_uids;
} g_deviceState;

//
//Helpers
//

static void resp_code(struct send_message_req *msg, int code)
{
	if (msg->resp_code)
		*msg->resp_code = code;
	signetdev_priv_message_send_resp(msg, 0, 0);
}

static void resp_data_multi(struct send_message_req *msg, int expected_messages_remaining, int code, int len)
{
	if (msg->resp_code)
		*msg->resp_code = code;
	signetdev_priv_message_send_resp(msg, len, expected_messages_remaining);
}

static void resp_data(struct send_message_req *msg, int code, int len)
{
	resp_data_multi(msg, 0, code, len);
}

static void enter_state(int state)
{
	g_deviceState.state = state;
	g_deviceState.progress_level = 0;
	g_deviceState.n_progress_components = 0;
}

static void enter_progressing_state(int state, int components, int *maximums)
{
	g_deviceState.state = state;
	g_deviceState.progress_level = 0;
	g_deviceState.n_progress_components = components;
	for (int i = 0; i < components; i++) {
		g_deviceState.progress_levels[i] = 0;
		g_deviceState.progress_maximums[i] = maximums[i];
	}
}

//
// Database helpers
//


static void derive_iv(u32 id, u8 *iv)
{
	struct root_page *root_page = (struct root_page *)g_deviceState.device_data[0];
	switch(root_page->signature[0]) {
	case 2:
		memcpy(iv, root_page->header.v2.cbc_iv, AES_BLK_SIZE);
		break;
	}
	iv[AES_BLK_SIZE-1] += (u8)(id);
}

static int get_block_header_size(int part_count)
{
	return SUB_BLK_COUNT(sizeof(struct db_block_header) + (sizeof(struct db_uid_ent) * part_count));
}

struct db_block *uid_to_db_block(int uid)
{
	int block_index = g_deviceState.uid_map[uid].block;
	if (block_index == 0) {
		return NULL;
	} else {
		return (struct db_block *)g_deviceState.device_data[block_index];
	}
}

struct db_uid_ent *uid_to_db_ent(int uid)
{
	struct db_block *block = uid_to_db_block(uid);
	if (block) {
		return block->uid_tbl + g_deviceState.uid_map[uid].index;
	} else {
		return NULL;
	}
}

//Returns the number of partitions a block can hold if it's partition size is 'part_size' sub blocks
static int get_part_count(int part_size)
{
	int max_sub_blocks = (BLK_SIZE/SUB_BLK_SIZE);
	int count = max_sub_blocks/part_size;
	while (count && ((get_block_header_size(count) + (count * part_size))) > max_sub_blocks) {
		count--;
	}
	return count;
}

static u8 *get_part(const struct db_block *block, int index)
{
	int part_count = get_part_count(block->header.part_size);
	int part_tbl_offs = get_block_header_size(part_count);
	return ((u8 *)block) + ((part_tbl_offs + (block->header.part_size * index)) * SUB_BLK_SIZE);
}

static void mask_uid_data(u8 *data, int blk_count)
{
	u8 *sub_block = data;
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

static int decode_uid(int uid, int masked, u8 *dest)
{
	struct db_block *blk = uid_to_db_block(uid);
	int index = g_deviceState.uid_map[uid].index;
	int blk_count = SIZE_TO_SUB_BLK_COUNT(blk->uid_tbl[index].sz);
	u8 iv[AES_BLK_SIZE];
	derive_iv(uid, iv);
	gcry_cipher_reset(g_deviceState.aes256_cbc);
	gcry_cipher_setiv(g_deviceState.aes256_cbc, iv, AES_BLK_SIZE);
	gcry_cipher_decrypt(g_deviceState.aes256_cbc, dest, AES_BLK_SIZE * blk_count, get_part(blk, index), AES_BLK_SIZE * blk_count);
	if (masked) {
		mask_uid_data(dest, blk_count);
	}
	return blk_count;
}

//
// Command implementations
//

static void get_progress_check(struct send_message_req *msg)
{
	if (g_deviceState.state != g_deviceState.progress_state) {
		resp_code(msg, INVALID_STATE);
	} else {
		if (g_deviceState.progress_level > g_deviceState.progress_check) {
			for (int i = 0; i < g_deviceState.n_progress_components; i++) {
				msg->resp[i * 4 + 0] = g_deviceState.progress_levels[i] & 0xff;
				msg->resp[i * 4 + 1] = g_deviceState.progress_levels[i] >> 8;
				msg->resp[i * 4 + 2] = g_deviceState.progress_maximums[i] & 0xff;
				msg->resp[i * 4 + 3] = g_deviceState.progress_maximums[i] >> 8;
			}
			resp_data(msg, OKAY, g_deviceState.n_progress_components * 4);
		}
	}
}

static int db_scan()
{
	int i;
	for (i = MIN_UID; i <= MAX_UID; i++)
		g_deviceState.uid_map[i].block = INVALID_BLOCK;
	for (int i = MIN_DATA_BLOCK; i <= MAX_DATA_BLOCK; i++) {
		struct db_block *blk = (struct db_block *)g_deviceState.device_data[i];
		int part_size = blk->header.part_size;
		int occupancy = blk->header.occupancy;
		if (part_size && part_size != INVALID_PART_SIZE && blk->header.crc != INVALID_CRC) {
			for (int j = 0; j < occupancy; j++) {
				const struct db_uid_ent *ent = blk->uid_tbl + j;
				int uid = ent->info & 0xfff;
				int first = (ent->info >> 14) & 1;
				int rev = (ent->info >> 12) & 3;
				if (uid >= MIN_UID && uid <= MAX_UID && first) {
					if (g_deviceState.uid_map[uid].block != INVALID_BLOCK) {
						//UID collision
						struct db_block *blk = uid_to_db_block(uid);
						struct db_uid_ent *prev_ent = blk->uid_tbl + g_deviceState.uid_map[uid].index;
						int prev_rev = (prev_ent->info >> 12) & 3;
						if (((rev + 1) & 3) == prev_rev) {
							g_deviceState.uid_map[uid].block = i;
							g_deviceState.uid_map[uid].index = j;
						}
					} else {
						g_deviceState.uid_map[uid].block = i;
						g_deviceState.uid_map[uid].index = j;
					}
				}
			}
		} else {
			blk->header.part_size = INVALID_PART_SIZE;
		}
	}
	return 0;
}

static void startup_cmd(struct send_message_req *msg)
{
	static u8 root_signature[AES_128_KEY_SIZE] = {3, 4, 5, 6,
						       7, 8, 9, 10,
						       11, 12, 13, 14,
						       15, 0, 0};
	struct root_page *root_page = (struct root_page *)g_deviceState.device_data[0];
	int rc = memcmp(root_page->signature + 1, root_signature, AES_BLK_SIZE - 1);
	if (rc != 0) {
		enter_state(UNINITIALIZED);
	} else {
		enter_state(LOGGED_OUT);
	}
	g_deviceState.header_version = root_page->signature[0];

	msg->resp[0] = SIGNET_MAJOR_VERSION;
	msg->resp[1] = SIGNET_MINOR_VERSION;
	msg->resp[2] = SIGNET_STEP_VERSION;
	msg->resp[3] = g_deviceState.state;
	msg->resp[4] = g_deviceState.header_version;
	msg->resp[5] = 0;
	if (g_deviceState.state == UNINITIALIZED) {
		signetdev_priv_message_send_resp(msg, OKAY, 0);
	} else {
		g_deviceState.db_version = root_page->header.v2.db_version;
		switch(g_deviceState.db_version) {
		case 2:
			if (msg->resp_code)
				*msg->resp_code = OKAY;
			db_scan();
			memcpy(msg->resp + 6, root_page->header.v2.hashfn, HASH_FN_SZ);
			memcpy(msg->resp + 6 + HASH_FN_SZ, root_page->header.v2.salt, SALT_SZ_V2);
			signetdev_priv_message_send_resp(msg, STARTUP_RESP_SIZE, 0);
		default:
			if (msg->resp_code)
				*msg->resp_code = UNKNOWN_DB_FORMAT;
			signetdev_priv_message_send_resp(msg, STARTUP_RESP_SIZE, 0);
			break;
		}
	}
}

static void login_cmd(struct send_message_req *msg)
{
	switch(g_deviceState.state) {
	case LOGGED_OUT:
		break;
	default:
		resp_code(msg, INVALID_STATE);
		return;
	}
	u8 auth_rand_test[AES_256_KEY_SIZE];
	if (msg->payload_size < LOGIN_KEY_SZ) {
		signetdev_priv_message_send_resp(msg, INVALID_INPUT, 0);
		return;
	}
	struct root_page *root_page = (struct root_page *)g_deviceState.device_data[0];
	u8 iv[AES_256_KEY_SIZE];
	memset(iv, 0, AES_256_KEY_SIZE);
	gcry_cipher_setkey(g_deviceState.aes256_cbc, msg->payload, AES_256_KEY_SIZE);
	gcry_cipher_setiv(g_deviceState.aes256_cbc, iv, AES_BLK_SIZE);
	gcry_cipher_encrypt(g_deviceState.aes256_cbc, auth_rand_test, AES_256_KEY_SIZE, root_page->header.v2.auth_rand, AES_256_KEY_SIZE);
	if (!memcmp(auth_rand_test, root_page->header.v2.auth_rand_ct, AES_256_KEY_SIZE)) {
		gcry_cipher_setiv(g_deviceState.aes256_cbc, iv, AES_BLK_SIZE);
		gcry_cipher_decrypt(g_deviceState.aes256_cbc, g_deviceState.encrypt_key, AES_256_KEY_SIZE, root_page->header.v2.encrypt_key_ct, AES_256_KEY_SIZE);
		gcry_cipher_setkey(g_deviceState.aes256_cbc, g_deviceState.encrypt_key, AES_256_KEY_SIZE);
		resp_code(msg, OKAY);
		enter_state(LOGGED_IN);
	} else {
		resp_code(msg, BAD_PASSWORD);
	}
}

static void logout_cmd(struct send_message_req *msg)
{
	switch(g_deviceState.state) {
	case LOGGED_IN:
		break;
	default:
		resp_code(msg, INVALID_STATE);
		return;
	}
	enter_state(LOGGED_OUT);
	resp_code(msg, OKAY);
}

static void get_device_state_cmd(struct send_message_req *msg)
{
	msg->resp[0] = g_deviceState.state;
	resp_data(msg, OKAY, 1);
}

static void get_progress_cmd(struct send_message_req *msg)
{
	if (msg->payload_size < 4) {
		resp_code(msg, INVALID_INPUT);
		return;
	}
	g_deviceState.progress_check = msg->resp[0] + (msg->resp[1] << 8);
	g_deviceState.progress_state = msg->resp[2] + (msg->resp[3] << 8);

	if (g_deviceState.state != g_deviceState.progress_state) {
		resp_code(msg, INVALID_STATE);
	} else {
		get_progress_check(msg);
	}
}

static void update_uid_cmd(struct send_message_req *msg)
{
	switch(g_deviceState.state) {
	case LOGGED_IN:
		break;
	default:
		resp_code(msg, INVALID_STATE);
		return;
	}
	if (msg->payload_size < 4) {
		resp_code(msg, INVALID_INPUT);
		return;
	}
	int uid = msg->resp[0] + (msg->resp[1] << 8);
	int sz = msg->resp[2] + (msg->resp[3] << 8);
	(void)(uid);
	(void)(sz);
	//TODO
}

static void read_uid_cmd(struct send_message_req *msg)
{
	switch(g_deviceState.state) {
	case LOGGED_IN:
		break;
	default:
		resp_code(msg, INVALID_STATE);
		return;
	}
	if (msg->payload_size < 3) {
		resp_code(msg, INVALID_INPUT);
		return;
	}
	int uid = msg->payload[0] + (msg->payload[1] << 8);
	int masked = msg->payload[2];
	if (uid < MIN_UID || uid > MAX_UID) {
		resp_code(msg, INVALID_INPUT);
		return;
	}
	struct db_uid_mapping *map = g_deviceState.uid_map + uid;
	if (!map->block) {
		resp_code(msg, ID_INVALID);
		return;
	}
	struct db_uid_ent *ent = uid_to_db_ent(uid);

	msg->resp[0] = (ent->sz) & 0xff;
	msg->resp[1] = (ent->sz) >> 8;
	int blk_count = decode_uid(uid, masked, msg->resp + 2);
	resp_data(msg, OKAY, (blk_count * SUB_BLK_SIZE) + 2);
}

static int read_all_uids_cmd_iter(struct send_message_req *msg)
{
	g_deviceState.read_all_uids.uid++;
	while (g_deviceState.uid_map[g_deviceState.read_all_uids.uid].block == INVALID_BLOCK && g_deviceState.read_all_uids.uid <= MAX_UID)
		g_deviceState.read_all_uids.uid++;
	int uid = g_deviceState.read_all_uids.uid;

	if (uid > MAX_UID) {
		msg->resp[0] = uid & 0xff;
		msg->resp[1] = uid >> 8;
		resp_data(msg, ID_INVALID, 2);
		return 0;
	}
	g_deviceState.read_all_uids.expected_remaining--;

	struct db_block *blk = uid_to_db_block(uid);
	int index = g_deviceState.uid_map[uid].index;
	int sz = blk->uid_tbl[index].sz;
	memset(msg->resp, 0, BLK_SIZE);
	msg->resp[0] = uid & 0xff;
	msg->resp[1] = uid >> 8;
	msg->resp[2] = sz & 0xff;
	msg->resp[3] = sz >> 8;
	int blk_count = decode_uid(uid, g_deviceState.read_all_uids.masked, msg->resp + 4);
	resp_data_multi(msg, g_deviceState.read_all_uids.expected_remaining + 1, OKAY, (blk_count * SUB_BLK_SIZE) + 4);
	return 1;
}

static void read_all_uids_cmd(struct send_message_req *msg)
{
	switch(g_deviceState.state) {
	case LOGGED_IN:
		break;
	default:
		resp_code(msg, INVALID_STATE);
		return;
	}
	if (msg->payload_size < 1) {
		resp_code(msg, INVALID_INPUT);
		return;
	}
	int masked = msg->payload[0];
	g_deviceState.read_all_uids.uid = 0;
	g_deviceState.read_all_uids.masked = masked;
	g_deviceState.read_all_uids.expected_remaining = 0;
	for (int i = MIN_UID; i <= MAX_UID; i++)
		if(g_deviceState.uid_map[i].block != INVALID_BLOCK)
			g_deviceState.read_all_uids.expected_remaining++;
	while (read_all_uids_cmd_iter(msg));
}

static void disconnect_cmd(struct send_message_req *msg)
{
	enter_state(DISCONNECTED);
	resp_code(msg, OKAY);
}

//
// signetdev_emulate_*
//

int signetdev_emulate_handle_message_priv(struct send_message_req *msg)
{
	switch (msg->dev_cmd) {
	//NOP operations
	case TYPE:
	case BUTTON_WAIT:
	case CANCEL_BUTTON_PRESS:
		resp_code(msg, OKAY);
		break;

	//Does not apply
	case ERASE_FLASH_PAGES:
		break;
	case WRITE_FLASH:
		break;
	case WRITE_BLOCK:
		break;
	case ERASE_BLOCK:
		break;
	case RESTORE_DEVICE_DONE:
		break;
	case UPDATE_FIRMWARE:
		break;
	case WIPE:
		break;
	case BACKUP_DEVICE:
		break;
	case READ_BLOCK:
		break;
	case GET_RAND_BITS:
		break;
	case ENTER_MOBILE_MODE:
		break;

	//Not implemented (properly) in hardware
	case GET_DEVICE_CAPACITY:
		break;

	//Not needed in software yet
	case LOGIN_TOKEN:
		break;
	case READ_CLEARTEXT_PASSWORD:
		break;
	case READ_CLEARTEXT_PASSWORD_NAMES:
		break;
	case WRITE_CLEARTEXT_PASSWORD:
		break;


	//Needed soon
	case INITIALIZE:
		break;
	case CHANGE_MASTER_PASSWORD:
		break;

	//Essential to emulate
	case STARTUP:
		startup_cmd(msg);
		break;
	case DISCONNECT:
		disconnect_cmd(msg);
		break;
	case LOGIN:
		login_cmd(msg);
		break;
	case LOGOUT:
		logout_cmd(msg);
		break;
	case GET_PROGRESS:
		get_progress_cmd(msg);
		break;
	case GET_DEVICE_STATE:
		get_device_state_cmd(msg);
		break;
	case UPDATE_UID:
		update_uid_cmd(msg);
		break;
	case READ_UID:
		read_uid_cmd(msg);
		break;
	case READ_ALL_UIDS:
		read_all_uids_cmd(msg);
		break;
	}
	signetdev_priv_free_message(&msg);
	return 0;
}

int signetdev_emulate_init(const char *filename)
{
	g_deviceState.state = DISCONNECTED;
	g_deviceState.db_file = fopen(filename, "r+b");
	g_deviceState.aes256_ecb = NULL;
	g_deviceState.aes256_ecb = NULL;
	g_deviceState.n_progress_components = 0;
	gcry_error_t err1 = gcry_cipher_open(&g_deviceState.aes256_ecb, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
	gcry_error_t err2 = gcry_cipher_open(&g_deviceState.aes256_cbc, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CBC, 0);
	if (g_deviceState.db_file) {
		fseek(g_deviceState.db_file, 0, SEEK_END);
		int len = ftell(g_deviceState.db_file);
		if (len != TOTAL_STORAGE_SIZE) {
			return 0;
		}
		fseek(g_deviceState.db_file, SEEK_SET, 0);
		int rc = fread(g_deviceState.device_data, BLK_SIZE, NUM_STORAGE_BLOCKS, g_deviceState.db_file);
		if (rc != NUM_STORAGE_BLOCKS) {
			return 0;
		}
	}
	return g_deviceState.db_file != NULL && err1 == GPG_ERR_NO_ERROR && err2 == GPG_ERR_NO_ERROR;
}

int signetdev_emulate_begin()
{
	return signetdev_priv_issue_command(SIGNETDEV_CMD_EMULATE_BEGIN, NULL);
}

void signetdev_emulate_end()
{
	return signetdev_priv_issue_command_no_resp(SIGNETDEV_CMD_EMULATE_END, NULL);
}

void signetdev_emulate_deinit()
{
	g_deviceState.state = DISCONNECTED;
	if (g_deviceState.db_file)
		fclose(g_deviceState.db_file);
	if (g_deviceState.aes256_ecb)
		gcry_cipher_close(g_deviceState.aes256_ecb);
	if (g_deviceState.aes256_cbc)
		gcry_cipher_close(g_deviceState.aes256_cbc);
}
