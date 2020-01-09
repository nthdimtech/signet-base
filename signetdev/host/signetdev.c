#include "signetdev.h"
#include "signetdev_priv.h"
#include "../common/signetdev_common.h"
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

void (*g_device_opened_cb)(void *) = NULL;
void *g_device_opened_cb_param = NULL;

void (*g_device_closed_cb)(void *) = NULL;
void *g_device_closed_cb_param = NULL;

static signetdev_cmd_resp_t g_command_resp_cb = NULL;
static void *g_command_resp_cb_param = NULL;

static signetdev_device_event_t g_device_event_cb = NULL;
static void *g_device_event_cb_param = NULL;

signetdev_conn_err_t g_error_handler = NULL;
void *g_error_handler_param = NULL;

void signetdev_set_device_opened_cb(void (*device_opened)(void *), void *param)
{
	g_device_opened_cb = device_opened;
	g_device_opened_cb_param = param;
}

void signetdev_set_device_closed_cb(void (*device_closed)(void *), void *param)
{
	g_device_closed_cb = device_closed;
	g_device_closed_cb_param = param;
}

void signetdev_set_command_resp_cb(signetdev_cmd_resp_t cb, void *cb_param)
{
	g_command_resp_cb = cb;
	g_command_resp_cb_param = cb_param;
}

void signetdev_set_device_event_cb(signetdev_device_event_t cb, void *cb_param)
{
	g_device_event_cb = cb;
	g_device_event_cb_param = cb_param;
}

void signetdev_set_error_handler(signetdev_conn_err_t handler, void *param)
{
	g_error_handler = handler;
	g_error_handler_param = param;
}

static struct signetdev_key keymap_inv[1<<16];
static struct signetdev_key keymap[1<<16];
static int keymap_num_keys;

static struct signetdev_key keymap_en_us[] = {

{'a', {{4 , 0} ,{0, 0}}}, {'A', {{4 , 2},{0, 0}}},
{'b', {{5 , 0} ,{0, 0}}}, {'B', {{5 , 2},{0, 0}}},
{'c', {{6 , 0} ,{0, 0}}}, {'C', {{6 , 2},{0, 0}}},
{'d', {{7 , 0} ,{0, 0}}}, {'D', {{7 , 2},{0, 0}}},
{'e', {{8 , 0} ,{0, 0}}}, {'E', {{8 , 2},{0, 0}}},
{'f', {{9 , 0} ,{0, 0}}}, {'F', {{9 , 2},{0, 0}}},
{'g', {{10, 0} ,{0, 0}}}, {'G', {{10, 2},{0, 0}}},
{'h', {{11, 0} ,{0, 0}}}, {'H', {{11, 2},{0, 0}}},
{'i', {{12, 0} ,{0, 0}}}, {'I', {{12, 2},{0, 0}}},
{'j', {{13, 0} ,{0, 0}}}, {'J', {{13, 2},{0, 0}}},
{'k', {{14, 0} ,{0, 0}}}, {'K', {{14, 2},{0, 0}}},
{'l', {{15, 0} ,{0, 0}}}, {'L', {{15, 2},{0, 0}}},
{'m', {{16, 0} ,{0, 0}}}, {'M', {{16, 2},{0, 0}}},
{'n', {{17, 0} ,{0, 0}}}, {'N', {{17, 2},{0, 0}}},
{'o', {{18, 0} ,{0, 0}}}, {'O', {{18, 2},{0, 0}}},
{'p', {{19, 0} ,{0, 0}}}, {'P', {{19, 2},{0, 0}}},
{'q', {{20, 0} ,{0, 0}}}, {'Q', {{20, 2},{0, 0}}},
{'r', {{21, 0} ,{0, 0}}}, {'R', {{21, 2},{0, 0}}},
{'s', {{22, 0} ,{0, 0}}}, {'S', {{22, 2},{0, 0}}},
{'t', {{23, 0} ,{0, 0}}}, {'T', {{23, 2},{0, 0}}},
{'u', {{24, 0} ,{0, 0}}}, {'U', {{24, 2},{0, 0}}},
{'v', {{25, 0} ,{0, 0}}}, {'V', {{25, 2},{0, 0}}},
{'w', {{26, 0} ,{0, 0}}}, {'W', {{26, 2},{0, 0}}},
{'x', {{27, 0} ,{0, 0}}}, {'X', {{27, 2},{0, 0}}},
{'y', {{28, 0} ,{0, 0}}}, {'Y', {{28, 2},{0, 0}}},
{'z', {{29, 0} ,{0, 0}}}, {'Z', {{29, 2},{0, 0}}},

{'1', {{30, 0} ,{0, 0}}}, {'!', {{30, 2},{0, 0}}},
{'2', {{31, 0} ,{0, 0}}}, {'@', {{31, 2},{0, 0}}},
{'3', {{32, 0} ,{0, 0}}}, {'#', {{32, 2},{0, 0}}},
{'4', {{33, 0} ,{0, 0}}}, {'$', {{33, 2},{0, 0}}},
{'5', {{34, 0} ,{0, 0}}}, {'%', {{34, 2},{0, 0}}},
{'6', {{35, 0} ,{0, 0}}}, {'^', {{35, 2},{0, 0}}},
{'7', {{36, 0} ,{0, 0}}}, {'&', {{36, 2},{0, 0}}},
{'8', {{37, 0} ,{0, 0}}}, {'*', {{37, 2},{0, 0}}},
{'9', {{38, 0} ,{0, 0}}}, {'(', {{38, 2},{0, 0}}},
{'0', {{39, 0} ,{0, 0}}}, {')', {{39, 2},{0, 0}}},
{'\n', {{40, 0} ,{0, 0}}},

{'\t', {{43, 0} ,{0, 0}}},
{' ' , {{44, 0} ,{0, 0}}},
{'-' , {{45, 0},{0, 0}}}, {'_', {{45, 2},{0,0}}},
{'=' , {{46, 0},{0, 0}}}, {'+', {{46, 2},{0,0}}},
{'[' , {{47, 0},{0, 0}}}, {'{', {{47, 2},{0,0}}},
{']' , {{48, 0},{0, 0}}}, {'}', {{48, 2},{0,0}}},
{'\\', {{49, 0},{0, 0}}}, {'|', {{49, 2},{0,0}}},

{';' , {{51, 0},{0, 0}}}, {':', {{51, 2},{0,0}}},
{'\'', {{52, 0},{0, 0}}}, {'"', {{52, 2},{0,0}}},
{'`' , {{53, 0},{0, 0}}}, {'~', {{53, 2},{0,0}}},
{',', {{54, 0},{0, 0}}},  {'<', {{54, 2},{0,0}}},
{'.', {{55, 0},{0, 0}}},  {'>', {{55, 2},{0,0}}},
{'/', {{56, 0},{0, 0}}},  {'?', {{56, 2},{0,0}}}
};

static int keymap_en_us_n_keys = sizeof(keymap_en_us) / sizeof (struct signetdev_key);

void signetdev_initialize_api()
{
	signetdev_set_keymap(keymap_en_us, keymap_en_us_n_keys);
	signetdev_priv_platform_init();
}

void signetdev_set_keymap(const struct signetdev_key *keys, int n_keys)
{
	for (unsigned int i = 0; i < (1<<16); i++) {
		keymap_inv[i].key = (u16)i;
		keymap_inv[i].phy_key[0].scancode = 0;
		keymap_inv[i].phy_key[0].modifier = 0;
	}
	for (int i = n_keys - 1; i >= 0; i--) {
		keymap[i] = keys[i];
		keymap_inv[keys[i].key] = keys[i];
	}
	keymap_num_keys = n_keys;
}

const struct signetdev_key *signetdev_get_keymap(int *n_keys)
{
	*n_keys = keymap_num_keys;
	return keymap;
}

void signetdev_deinitialize_api()
{
	signetdev_priv_platform_deinit();
}

int signetdev_cancel_button_wait()
{
    signetdev_priv_cancel_message(CANCEL_BUTTON_PRESS, NULL, 0);
    return OKAY;
}

static int get_cmd_token()
{
	static int token_ctr = 0;
	return token_ctr++;
}

static int execute_command(void *user, int token, int dev_cmd, int api_cmd)
{
	return signetdev_priv_send_message(user, token, dev_cmd,
			api_cmd,
			0, NULL, 0 /* payload size*/,
			SIGNETDEV_PRIV_GET_RESP);
}

static int execute_command_no_resp(void *user, int token,  int dev_cmd, int api_cmd)
{
	return signetdev_priv_send_message(user, token, dev_cmd,
			api_cmd,
			0, NULL, 0,
			SIGNETDEV_PRIV_NO_RESP);
}

int signetdev_logout(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token, LOGOUT, SIGNETDEV_CMD_LOGOUT);
}

int signetdev_wipe(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		WIPE, SIGNETDEV_CMD_WIPE);
}

int signetdev_button_wait(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		BUTTON_WAIT, SIGNETDEV_CMD_BUTTON_WAIT);
}

int signetdev_get_rand_bits(void *user, int *token, int sz)
{
	uint8_t msg[2];
	msg[0] = sz & 0xff;
	msg[1] = sz >> 8;
	return signetdev_priv_send_message(user, *token,
		GET_RAND_BITS, SIGNETDEV_CMD_GET_RAND_BITS,
		0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_disconnect(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command_no_resp(user, *token,
		DISCONNECT, SIGNETDEV_CMD_DISCONNECT);
}

int signetdev_login(void *user, int *token, u8 *key, unsigned int key_len, int gen_token)
{
	*token = get_cmd_token();
	uint8_t msg[AES_256_KEY_SIZE + 1];
	memset(msg, 0, sizeof(msg));
	memcpy(msg, key, key_len > AES_256_KEY_SIZE ? sizeof(msg) : key_len);
	msg[AES_256_KEY_SIZE] = gen_token;
	return signetdev_priv_send_message(user, *token,
		LOGIN, SIGNETDEV_CMD_LOGIN,
		0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_login_token(void *user, int *api_token, u8 *token)
{
	*api_token = get_cmd_token();
	uint8_t msg[AES_256_KEY_SIZE];
	memcpy(msg, token, sizeof(msg));
	return signetdev_priv_send_message(user, *token,
		LOGIN_TOKEN, SIGNETDEV_CMD_LOGIN_TOKEN,
		0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_get_progress(void *user, int *token, int progress, int state)
{
	uint8_t msg[4];
	*token = get_cmd_token();
	msg[0] = progress & 0xff;
	msg[1] = progress >> 8;
	msg[2] = state & 0xff;
	msg[3] = state >> 8;
	return signetdev_priv_send_message(user, *token,
			GET_PROGRESS, SIGNETDEV_CMD_GET_PROGRESS,
			0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_begin_device_backup(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		BACKUP_DEVICE, SIGNETDEV_CMD_BEGIN_DEVICE_BACKUP);
}

int signetdev_end_device_backup(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		BACKUP_DEVICE_DONE, SIGNETDEV_CMD_END_DEVICE_BACKUP);
}

int signetdev_begin_device_restore(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		RESTORE_DEVICE, SIGNETDEV_CMD_BEGIN_DEVICE_RESTORE);
}

int signetdev_end_device_restore(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		RESTORE_DEVICE_DONE, SIGNETDEV_CMD_END_DEVICE_RESTORE);
}

int signetdev_get_device_state(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token, GET_DEVICE_STATE, SIGNETDEV_CMD_GET_DEVICE_STATE);
}

int signetdev_begin_update_firmware(void *user, int *token)
{
	*token = get_cmd_token();
	return execute_command(user, *token,
		UPDATE_FIRMWARE, SIGNETDEV_CMD_BEGIN_UPDATE_FIRMWARE);
}

int signetdev_begin_update_firmware_hc(void *user, int *token, const struct hc_firmware_info *fw_info)
{
	*token = get_cmd_token();

	return signetdev_priv_send_message(user, *token,
			UPDATE_FIRMWARE, SIGNETDEV_CMD_BEGIN_UPDATE_FIRMWARE,
			0, (const u8 *)fw_info, sizeof(*fw_info), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_reset_device(void *user, int *token)
{
	*token = get_cmd_token();
	int rc = execute_command_no_resp(user, *token,
		RESET_DEVICE, SIGNETDEV_CMD_RESET_DEVICE);
	return rc;
}

int signetdev_switch_boot_mode(void *user, int *token)
{
	*token = get_cmd_token();
	int rc = execute_command_no_resp(user, *token,
		SWITCH_BOOT_MODE, SIGNETDEV_CMD_SWITCH_BOOT_MODE);
	return rc;
}

int signetdev_startup(void *param, int *token)
{
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			STARTUP, SIGNETDEV_CMD_STARTUP,
			0, NULL, 0,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_has_keyboard()
{
	return signetdev_priv_issue_command(SIGNETDEV_CMD_HAS_KEYBOARD, NULL);
}

int signetdev_enter_mobile_mode(void *param, int *token)
{
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			ENTER_MOBILE_MODE, SIGNETDEV_CMD_ENTER_MOBILE_MODE,
			0, NULL, 0,
			SIGNETDEV_PRIV_NO_RESP);
}


int signetdev_can_type(const u8 *keys, int n_keys)
{
	for (int i = 0; i < n_keys; i++) {
		u8 c = keys[i];
		struct signetdev_key *key = keymap_inv + c;
		if (!key->phy_key[0].scancode) {
			return 0;
		}
	}
	return 1;
}

int signetdev_can_type_w(const u16 *keys, int n_keys)
{
	for (int i = 0; i < n_keys; i++) {
		u16 c = keys[i];
		struct signetdev_key *key = keymap_inv + c;
		if (!key->phy_key[0].scancode) {
			return 0;
		}
	}
	return 1;
}

int signetdev_read_cleartext_password(void *param, int *token, int index)
{
	u8 data[1] = {index};
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			READ_CLEARTEXT_PASSWORD, SIGNETDEV_CMD_READ_CLEARTEXT_PASSWORD,
			0, data, 1,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_read_cleartext_password_names(void *param, int *token)
{
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			READ_CLEARTEXT_PASSWORD_NAMES, SIGNETDEV_CMD_READ_CLEARTEXT_PASSWORD_NAMES,
			0, NULL, 0,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_write_cleartext_password(void *param, int *token, int index, const struct cleartext_pass *pass)
{
	u8 data[CLEARTEXT_PASS_SIZE + 1];
	data[0] = index;
	memcpy(data + 1, pass, CLEARTEXT_PASS_SIZE);
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			WRITE_CLEARTEXT_PASSWORD, SIGNETDEV_CMD_WRITE_CLEARTEXT_PASSWORD,
			0, data, CLEARTEXT_PASS_SIZE + 1,
			SIGNETDEV_PRIV_GET_RESP);
}


int signetdev_to_scancodes_w(const u16 *keys, int n_keys, u16 *out, int *out_len_)
{
	u8 prev_scancode = 0;
	int i = 0;
	int out_len = *out_len_;
	while (i < n_keys && out_len >= 2) {
		u16 c = keys[i];
		struct signetdev_key *key = keymap_inv + c;
		if (key->phy_key[0].scancode && prev_scancode == key->phy_key[0].scancode) {
			out[0] = 0;
			out ++;
			out_len--;
			prev_scancode = 0;
			continue;
		}
		if (key->phy_key[0].scancode) {
			if (key->phy_key[1].scancode && out_len <= 3) {
				break;
			}
			out[0] = key->phy_key[0].modifier + (key->phy_key[0].scancode << 8);
			out_len--; out++;
			if (key->phy_key[1].scancode) {
				out[0] = key->phy_key[1].modifier + (key->phy_key[1].scancode << 8);
				out_len--; out++;
			}
		} else {
			return 2;
		}
		prev_scancode = (out - 1)[0] >> 8;
		i++;
	}
	out[0] = 0; out_len--; out++;
	*out_len_ = (*out_len_) - out_len;
	return (i == n_keys) ? 0 : 1;
}


int signetdev_type(void *param, int *token, const u8 *keys, int n_keys)
{
	*token = get_cmd_token();
	u8 msg[CMD_PACKET_PAYLOAD_SIZE];
	int i;
	int j = 0;
	unsigned int message_size = 0;
	for (i = 0; i < n_keys; i++) {
		u8 c = keys[i];
		struct signetdev_key *key = keymap_inv + c;
		if (key->phy_key[0].scancode) {
			if (key->phy_key[1].scancode) {
				message_size += 8;
			} else {
				message_size += 4;
			}
			if (message_size >= sizeof(msg))
				return SIGNET_ERROR_OVERFLOW;

			if (key->phy_key[1].scancode) {
				msg[j * 2 + 0] = key->phy_key[0].modifier;
				msg[j * 2 + 1] = key->phy_key[0].scancode;
				msg[j * 2 + 2] = 0;
				msg[j * 2 + 3] = 0;
				msg[j * 2 + 4] = key->phy_key[1].modifier;
				msg[j * 2 + 5] = key->phy_key[1].scancode;
				msg[j * 2 + 6] = 0;
				msg[j * 2 + 7] = 0;
				j += 4;
			} else {
				msg[j * 2 + 1] = key->phy_key[0].scancode;
				msg[j * 2 + 0] = key->phy_key[0].modifier;
				msg[j * 2 + 2] = 0;
				msg[j * 2 + 3] = 0;
				j += 2;
			}
		}
	}
	return signetdev_priv_send_message(param, *token,
			TYPE, SIGNETDEV_CMD_TYPE,
			0, msg, message_size,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_type_w(void *param, int *token, const u16 *keys, int n_keys)
{
	*token = get_cmd_token();
	u8 msg[CMD_PACKET_PAYLOAD_SIZE];
	int i;
	int j = 0;
	unsigned int message_size = 0;
	for (i = 0; i < n_keys; i++) {
		u16 c = keys[i];
		struct signetdev_key *key = keymap_inv + c;
		if (key->phy_key[0].scancode) {
			if (key->phy_key[1].scancode) {
				message_size += 8;
			} else {
				message_size += 4;
			}
			if (message_size >= sizeof(msg))
				return SIGNET_ERROR_OVERFLOW;

			if (key->phy_key[1].scancode) {
				msg[j * 2 + 0] = key->phy_key[0].modifier;
				msg[j * 2 + 1] = key->phy_key[0].scancode;
				msg[j * 2 + 2] = 0;
				msg[j * 2 + 3] = 0;
				msg[j * 2 + 4] = key->phy_key[1].modifier;
				msg[j * 2 + 5] = key->phy_key[1].scancode;
				msg[j * 2 + 6] = 0;
				msg[j * 2 + 7] = 0;
				j += 4;
			} else {
				msg[j * 2 + 1] = key->phy_key[0].scancode;
				msg[j * 2 + 0] = key->phy_key[0].modifier;
				msg[j * 2 + 2] = 0;
				msg[j * 2 + 3] = 0;
				j += 2;
			}
		}
	}
	return signetdev_priv_send_message(param, *token,
			TYPE, SIGNETDEV_CMD_TYPE,
			0, msg, message_size,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_type_raw(void *param, int *token, const u8 *codes, int n_keys)
{
	*token = get_cmd_token();
	u8 msg[CMD_PACKET_PAYLOAD_SIZE];
	unsigned int message_size = (n_keys) * 2;
	if (message_size >= sizeof(msg))
		return SIGNET_ERROR_OVERFLOW;
	int i;
	for (i = 0; i < n_keys; i++) {
		msg[i * 2 + 0] = codes[i * 2];
		msg[i * 2 + 1] = codes[i * 2 + 1];
	}
	return signetdev_priv_send_message(param, *token,
			TYPE, SIGNETDEV_CMD_TYPE,
			0, msg, message_size,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_begin_initialize_device(void *param, int *token,
					const u8 *key, int key_len,
					const u8 *hashfn, int hashfn_len,
					const u8 *salt, int salt_len,
					const u8 *rand_data, int rand_data_len)
{
	*token = get_cmd_token();
	u8 msg[CMD_PACKET_PAYLOAD_SIZE];
	memset(msg, 0, INITIALIZE_CMD_SIZE); //Hash function

	memcpy(msg, key, key_len > AES_256_KEY_SIZE ? AES_256_KEY_SIZE : key_len);
	memcpy(msg + AES_256_KEY_SIZE, hashfn, hashfn_len > HASH_FN_SZ ? HASH_FN_SZ : hashfn_len);
	memcpy(msg + AES_256_KEY_SIZE + HASH_FN_SZ, salt, salt_len > SALT_SZ_V2 ? SALT_SZ_V2 : salt_len);
	memcpy(msg + AES_256_KEY_SIZE + HASH_FN_SZ + SALT_SZ_V2, rand_data, rand_data_len > INIT_RAND_DATA_SZ ? INIT_RAND_DATA_SZ : rand_data_len);

	return signetdev_priv_send_message(param, *token,
			INITIALIZE, SIGNETDEV_CMD_BEGIN_INITIALIZE_DEVICE,
			0, msg, INITIALIZE_CMD_SIZE, SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_read_block(void *param, int *token, unsigned int idx)
{
	*token = get_cmd_token();
	u8 msg[] = {(u8)idx};
	return signetdev_priv_send_message(param, *token,
			READ_BLOCK, SIGNETDEV_CMD_READ_BLOCK,
			0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_write_block(void *param, int *token, unsigned int idx, const void *buffer)
{
	*token = get_cmd_token();
	u8 msg[BLK_SIZE + 1] = {(u8)idx};
	memcpy(msg + 1, buffer, BLK_SIZE);
	return signetdev_priv_send_message(param, *token,
				WRITE_BLOCK, SIGNETDEV_CMD_WRITE_BLOCK,
				0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_write_flash(void *param, int *token, u32 addr, const void *data, unsigned int data_len)
{
	*token = get_cmd_token();
	uint8_t msg[CMD_PACKET_PAYLOAD_SIZE];
	unsigned int message_size = 4 + data_len;
	if (message_size >= sizeof(msg))
		return SIGNET_ERROR_OVERFLOW;
	msg[0] = (u8)(addr >> 0) & 0xff;
	msg[1] = (u8)(addr >> 8) & 0xff;
	msg[2] = (u8)(addr >> 16) & 0xff;
	msg[3] = (u8)(addr >> 24) & 0xff;
	memcpy(msg + 4, data, data_len);
	return signetdev_priv_send_message(param, *token,
				WRITE_FLASH, SIGNETDEV_CMD_WRITE_FLASH,
				0, msg, 4 + data_len, SIGNETDEV_PRIV_GET_RESP);
}

int encode_entry_data(unsigned int size, const u8 *data, const u8 *mask, uint8_t *msg, unsigned int msg_sz)
{
	unsigned int i;
	unsigned int blk_count = SIZE_TO_SUB_BLK_COUNT(size);
	unsigned int message_size = blk_count * SUB_BLK_SIZE;
	if (message_size >= msg_sz)
		return SIGNET_ERROR_OVERFLOW;

	for (i = 0; i < size; i++) {
		int r = i % SUB_BLK_DATA_SIZE;
		int blk = i / SUB_BLK_DATA_SIZE;
		int idx = blk * SUB_BLK_SIZE + r + SUB_BLK_MASK_SIZE;
		unsigned int bit = (mask[i/8] >> (i % 8)) & 0x1;
		int m_idx = blk * SUB_BLK_SIZE + (r/8);
		msg[idx] = data[i];
		msg[m_idx] = (u8)((msg[m_idx] & ~(1<<(r%8))) | (bit << (r%8)));
	}
	return (int)message_size;
}

int signetdev_update_uid(void *param, int *token, unsigned int uid, unsigned int size, const u8 *data, const u8 *mask)
{
	uint8_t msg[CMD_PACKET_PAYLOAD_SIZE];
	*token = get_cmd_token();
	int k = 0;
	msg[k] = (u8)(uid >> 0) & 0xff; k++;
	msg[k] = (u8)(uid >> 8) & 0xff; k++;
	msg[k] = (u8)(size >> 0) & 0xff; k++;
	msg[k] = (u8)(size >> 8); k++;
	int message_size = encode_entry_data(size, data, mask, msg + k, sizeof(msg) - (unsigned int)k);

	if (message_size < 0)
		return message_size;

	message_size += k;

	return signetdev_priv_send_message(param, *token,
		UPDATE_UID, SIGNETDEV_CMD_UPDATE_UID,
	        0, msg, (unsigned int)message_size, SIGNETDEV_PRIV_GET_RESP);
}


int signetdev_update_uids(void *param, int *token, unsigned int uid, unsigned int size, const u8 *data, const u8 *mask, unsigned int remaining_uids)
{
	uint8_t msg[CMD_PACKET_PAYLOAD_SIZE];
	*token = get_cmd_token();
	int k = 0;
	msg[k] = (u8)(uid >> 0) & 0xff; k++;
	msg[k] = (u8)(uid >> 8) & 0xff; k++;
	msg[k] = (u8)(size >> 0) & 0xff; k++;
	msg[k] = (u8)(size >> 8); k++;
	int message_size = encode_entry_data(size, data, mask, msg + k, sizeof(msg) - (unsigned int)k);

	if (message_size < 0)
		return message_size;

	message_size += k;

	return signetdev_priv_send_message(param, *token,
		UPDATE_UIDS, SIGNETDEV_CMD_UPDATE_UIDS,
	        remaining_uids, msg, (unsigned int)message_size, SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_read_uid(void *param, int *token, int uid, int masked)
{
	*token = get_cmd_token();
	uint8_t msg[3];
	msg[0] = (uid >> 0) & 0xff;
	msg[1] = (uid >> 8) & 0xff;
	msg[2] = (masked) & 0xff;
	return signetdev_priv_send_message(param, *token,
				READ_UID, SIGNETDEV_CMD_READ_UID,
				0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_read_all_uids(void *param, int *token, int masked)
{
	*token = get_cmd_token();
	uint8_t msg[1];
	msg[0] = masked & 0xff;
	return signetdev_priv_send_message(param, *token,
				READ_ALL_UIDS, SIGNETDEV_CMD_READ_ALL_UIDS,
				0, msg, sizeof(msg), SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_change_master_password(void *param, int *token,
		u8 *old_key, u32 old_key_len,
		u8 *new_key, u32 new_key_len,
		u8 *hashfn, u32 hashfn_len,
		u8 *salt, u32 salt_len)
{
	*token = get_cmd_token();
	uint8_t msg[AES_256_KEY_SIZE * 2 + HASH_FN_SZ + SALT_SZ_V2];
	memset(msg, 0, sizeof(msg));
	memcpy(msg, old_key, old_key_len > AES_256_KEY_SIZE ? AES_256_KEY_SIZE : old_key_len);
	memcpy(msg + AES_256_KEY_SIZE, new_key, new_key_len > AES_256_KEY_SIZE ? AES_256_KEY_SIZE : new_key_len);
	memcpy(msg + AES_256_KEY_SIZE * 2, hashfn, hashfn_len > HASH_FN_SZ ? HASH_FN_SZ : hashfn_len);
	memcpy(msg + AES_256_KEY_SIZE * 2 + HASH_FN_SZ, salt, salt_len > SALT_SZ_V2 ? SALT_SZ_V2 : salt_len);
	return signetdev_priv_send_message(param, *token, CHANGE_MASTER_PASSWORD,
			SIGNETDEV_CMD_CHANGE_MASTER_PASSWORD,
			0, msg, sizeof(msg),
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_erase_pages(void *param, int *token, unsigned int n_pages, const u8 *page_numbers)
{
	*token = get_cmd_token();
	return signetdev_priv_send_message(param, *token,
			ERASE_FLASH_PAGES, SIGNETDEV_CMD_ERASE_PAGES,
			0, page_numbers, n_pages,
			SIGNETDEV_PRIV_GET_RESP);
}

int signetdev_erase_pages_hc(void *param, int *token)
{
	*token = get_cmd_token();
	return execute_command(param, *token, ERASE_FLASH_PAGES, SIGNETDEV_CMD_ERASE_PAGES);
}

void signetdev_priv_handle_device_event(int event_type, const u8 *resp, int resp_len)
{
	if (g_device_event_cb) {
		g_device_event_cb(g_device_event_cb_param, event_type, (const void *)resp, resp_len);
	}
}

void signetdev_priv_prepare_message_state(struct tx_message_state *msg, unsigned int dev_cmd, unsigned int messages_remaining, u8 *payload, unsigned int payload_size)
{
	msg->msg_size = payload_size + CMD_PACKET_HEADER_SIZE;
	msg->msg_buf[0] = (u8)(msg->msg_size & 0xff);
	msg->msg_buf[1] = (u8)(msg->msg_size >> 8);
	msg->msg_buf[2] = (u8)(dev_cmd);
	msg->msg_buf[3] = (u8)(messages_remaining & 0xff);
	msg->msg_buf[4] = (u8)(messages_remaining >> 8);
	msg->msg_packet_seq = 0;
	msg->msg_packet_count = (msg->msg_size + RAW_HID_PAYLOAD_SIZE - 1)/ RAW_HID_PAYLOAD_SIZE;
	if (payload)
		memcpy(msg->msg_buf + CMD_PACKET_HEADER_SIZE, payload, payload_size);

}

void signetdev_priv_advance_message_state(struct tx_message_state *msg)
{
	int pidx = 0;
	msg->packet_buf[pidx++] = 0;
	msg->packet_buf[pidx++] = (u8)(msg->msg_packet_seq |
	                (((msg->msg_packet_seq + 1) == msg->msg_packet_count) ? 0x80 : 0));
	memcpy(msg->packet_buf + 1 + RAW_HID_HEADER_SIZE,
		   msg->msg_buf + RAW_HID_PAYLOAD_SIZE * msg->msg_packet_seq,
		   RAW_HID_PAYLOAD_SIZE);
	msg->msg_packet_seq++;
}


int signetdev_priv_message_packet_count(int msg_sz)
{
	return (msg_sz + RAW_HID_PAYLOAD_SIZE - 1)/ RAW_HID_PAYLOAD_SIZE;
}

static int decode_id(const u8 *resp, unsigned int resp_len, u8 *data, u8 *mask)
{
	unsigned int i;
	unsigned int blk_count = SUB_BLK_COUNT(resp_len);
	if ((SUB_BLK_SIZE * blk_count) >= CMD_PACKET_PAYLOAD_SIZE) {
		return -1;
	}
	if ((SUB_BLK_SIZE * blk_count) != resp_len) {
		return -1;
	}
	for (i = 0; i < resp_len; i++) {
		int blk = i / SUB_BLK_DATA_SIZE;
		int blk_field = (i % SUB_BLK_DATA_SIZE);
		int bit = (resp[(blk * SUB_BLK_SIZE) + (blk_field/8)] >> (blk_field%8)) & 1;
		data[i] = resp[(blk * SUB_BLK_SIZE) + blk_field + SUB_BLK_MASK_SIZE];
		mask[i/8] = (mask[i/8] & ~(1<<(i%8))) | (bit << (i%8));
	}
	return 0;
}

void signetdev_priv_handle_command_resp(void *user, int token,
					int dev_cmd, int api_cmd,
                                        int resp_code, const u8 *resp, unsigned int resp_len,
					int end_device_state,
					int expected_messages_remaining)
{
	switch (dev_cmd)
	{
	case READ_BLOCK: {
		if (resp_len != BLK_SIZE) {
			signetdev_priv_handle_error();
			break;
		} else if (g_command_resp_cb) {
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
			        resp_code, (const void *)resp);
		}
	} break;
	case READ_CLEARTEXT_PASSWORD:
		if (resp_code == OKAY && resp_len != CLEARTEXT_PASS_SIZE) {
			signetdev_priv_handle_error();
			break;
		} else if (g_command_resp_cb) {
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
			        resp_code, (const void *)resp);
		}
	break;
	case READ_CLEARTEXT_PASSWORD_NAMES:
		if (resp_code ==OKAY && resp_len != (CLEARTEXT_PASS_NAME_SIZE + 1) * NUM_CLEARTEXT_PASS) {
			signetdev_priv_handle_error();
			break;
		} else if (g_command_resp_cb) {
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
			        resp_code, (const void *)resp);
		}
	break;
	case GET_PROGRESS: {
		struct signetdev_get_progress_resp_data cb_resp;
		if (resp_code == OKAY && resp_len >= 4 && (resp_len % 4) == 0) {
			cb_resp.total_progress = resp[0] + (resp[1] << 8);
			cb_resp.total_progress_maximum = resp[2] + (resp[3] << 8);
			int k = (resp_len/4) - 1;
			int i;
			cb_resp.n_components = k;
			for (i = 0; i < k; i++) {
				int j = i + 1;
				cb_resp.progress[i] = resp[j*4] + (resp[j*4 + 1] << 8);
				cb_resp.progress_maximum[i] = resp[j*4 + 2] + (resp[j*4 + 3] << 8);
			}
		} else if (resp_code == INVALID_STATE) {
			cb_resp.n_components = 0;
			cb_resp.total_progress = 0;
			cb_resp.total_progress_maximum = 0;
		} else {
			signetdev_priv_handle_error();
			break;
		}
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
				resp_code, &cb_resp);

		} break;
	case STARTUP: {
		struct signetdev_startup_resp_data cb_resp;
		if (resp_code == OKAY && resp_len < STARTUP_RESP_SIZE) {
			signetdev_priv_handle_error();
			break;
		} else if (resp_code == OKAY || resp_code == UNKNOWN_DB_FORMAT) {
			cb_resp.fw_major_version = resp[0];
			cb_resp.fw_minor_version = resp[1];
			cb_resp.fw_step_version = resp[2];
			cb_resp.device_state = resp[3];
			cb_resp.root_block_format = resp[4];
			cb_resp.db_format = resp[5];
			cb_resp.boot_mode = resp[6];
			cb_resp.upgrade_state = resp[7];
			memcpy(cb_resp.hashfn, resp + STARTUP_RESP_INFO_SIZE, HASH_FN_SZ);
			memcpy(cb_resp.salt, resp + STARTUP_RESP_INFO_SIZE + HASH_FN_SZ, SALT_SZ_V2);
		}
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
				resp_code, &cb_resp);
		} break;
	case GET_RAND_BITS: {
		struct signetdev_get_rand_bits_resp_data cb_resp;
		cb_resp.data = resp;
		cb_resp.size = resp_len;
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
				resp_code, &cb_resp);
		} break;
	case READ_ALL_UIDS: {
		struct signetdev_read_all_uids_resp_data cb_resp;
		u8 *data = cb_resp.data;
		u8 *mask = cb_resp.mask;
		if (resp_code == OKAY) {
			if (resp_len < 4) {
				signetdev_priv_handle_error();
				break;
			}
			cb_resp.uid = resp[0] + (resp[1] << 8);
			cb_resp.size = resp[2] + (resp[3] << 8);
			resp_len -=4;
			resp += 4;
			if (resp_len) {
				if (decode_id(resp, resp_len, data, mask)) {
					signetdev_priv_handle_error();
					break;
				}
			}
		}
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
				resp_code, &cb_resp);
		} break;
	case READ_UID: {
		struct signetdev_read_uid_resp_data cb_resp;
		u8 *data = cb_resp.data;
		u8 *mask = cb_resp.mask;
		if (resp_code == OKAY) {
			if (resp_len < 2) {
				signetdev_priv_handle_error();
				break;
			}
			cb_resp.size = resp[0] + (resp[1] << 8);
			resp_len -=2;
			resp += 2;
			if (resp_len) {
				if (decode_id(resp, resp_len, data, mask)) {
					signetdev_priv_handle_error();
					break;
				}
			}
		}
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
				user, token, api_cmd,
				end_device_state,
				expected_messages_remaining,
				resp_code, &cb_resp);
		} break;
	default:
		if (g_command_resp_cb)
			g_command_resp_cb(g_command_resp_cb_param,
					  user, token, api_cmd,
					  end_device_state,
					  expected_messages_remaining,
					  resp_code, NULL);
		break;
	}
}

int signetdev_priv_send_message(void *user, int token, int dev_cmd, int api_cmd, unsigned int messages_remaining, const u8 *payload, unsigned int payload_size, int get_resp)
{
	static u8 s_async_resp[CMD_PACKET_PAYLOAD_SIZE];
	static int s_async_resp_code;

	struct send_message_req *r = (struct send_message_req *)malloc(sizeof(struct send_message_req));
	r->dev_cmd = dev_cmd;
	r->api_cmd = api_cmd;
	r->messages_remaining = messages_remaining;
	if (payload) {
		r->payload = malloc(payload_size);
		memcpy(r->payload, payload, payload_size);
	} else {
		r->payload = NULL;
	}
	r->payload_size = payload_size;
	r->user = user;
	r->token = token;
	if (get_resp) {
		r->resp = s_async_resp;
		r->resp_code = &s_async_resp_code;
	} else {
		r->resp = NULL;
		r->resp_code = NULL;
	}
	r->interrupt = 0;
	signetdev_priv_issue_command_no_resp(SIGNETDEV_CMD_MESSAGE, r);
	return 0;
}

int signetdev_priv_cancel_message(int dev_cmd, const u8 *payload, unsigned int payload_size)
{
	struct send_message_req *r = malloc(sizeof(struct send_message_req));
	r->dev_cmd = dev_cmd;
	if (payload) {
		r->payload = malloc(payload_size);
		memcpy(r->payload, payload, payload_size);
	} else {
		r->payload = NULL;
	}
	r->payload_size = payload_size;
	r->resp = NULL;
	r->resp_code = NULL;
	r->interrupt = 1;
	signetdev_priv_issue_command_no_resp(SIGNETDEV_CMD_CANCEL_MESSAGE, r);
	return 0;
}


void signetdev_priv_process_rx_packet(struct rx_message_state *state, u8 *rx_packet_buf)
{
	int seq = rx_packet_buf[0] & 0x7f;
	int last = rx_packet_buf[0] >> 7;
	if (seq > 30) {
		seq = seq;
	}
	const u8 *rx_packet_header = rx_packet_buf + RAW_HID_HEADER_SIZE;
	if (seq == 0x7f) {
		int event_type = rx_packet_header[0];
		int resp_len =  rx_packet_header[1];
		const void *data = (const void *)(rx_packet_header + 2);
		signetdev_priv_handle_device_event(event_type, data, resp_len);
	} else if (state->message) {
		if (seq == 0) {
			state->expected_resp_size = rx_packet_header[0] + ((unsigned int)rx_packet_header[1] << 8) - CMD_PACKET_HEADER_SIZE;
			state->expected_messages_remaining = rx_packet_header[3] + (rx_packet_header[4] << 8);
			if (state->message->resp_code) {
				*state->message->resp_code = rx_packet_header[2];
			}
			state->message->end_device_state = rx_packet_header[5];
			memcpy(state->message->resp,
				rx_packet_buf + RAW_HID_HEADER_SIZE + CMD_PACKET_HEADER_SIZE,
				RAW_HID_PAYLOAD_SIZE - CMD_PACKET_HEADER_SIZE);
		} else {
			size_t to_read = RAW_HID_PAYLOAD_SIZE;
			size_t offset = (RAW_HID_PAYLOAD_SIZE * (size_t)seq) - CMD_PACKET_HEADER_SIZE;
			if ((offset + to_read) > state->expected_resp_size) {
				to_read = (state->expected_resp_size - offset);
			}
			if (to_read > 0)
				memcpy(state->message->resp + offset, rx_packet_header, to_read);
		}
		if (last) {
			if (state->expected_messages_remaining == 0) {
				signetdev_priv_finalize_message(&state->message, (int)state->expected_resp_size);
			} else {
				signetdev_priv_message_send_resp(state->message, (int)state->expected_resp_size, state->expected_messages_remaining);
			}
		}
	}
}


void signetdev_priv_message_send_resp(struct send_message_req *msg, int rc, int expected_messages_remaining)
{
	if (!msg->interrupt) {
		int resp_code = OKAY;
		int resp_len = 0;
		if (msg->resp_code)
			resp_code = *msg->resp_code;
		if (rc >= 0)
			resp_len = rc;
		else
			resp_code = rc;
		signetdev_priv_handle_command_resp(msg->user,
			       msg->token,
			       msg->dev_cmd,
			       msg->api_cmd,
			       resp_code,
			       msg->resp,
			       resp_len,
			       msg->end_device_state,
			       expected_messages_remaining);
	}
}

void signetdev_priv_free_message(struct send_message_req **req)
{
	if ((*req)->payload)
		free((*req)->payload);
	free(*req);
	*req = NULL;
}

void signetdev_priv_finalize_message(struct send_message_req **msg ,int rc)
{
	signetdev_priv_message_send_resp(*msg, rc, 0);
	signetdev_priv_free_message(msg);
}
