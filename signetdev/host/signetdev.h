#ifndef SIGNETDEV_H
#define SIGNETDEV_H

#include "../common/signetdev_common.h"
#include "../common/signetdev_common_priv.h"
#include "../common/signetdev_hc_common.h"
#include <stdint.h>

void signetdev_initialize_api();

enum signetdev_device_type {
        SIGNETDEV_DEVICE_NONE,
        SIGNETDEV_DEVICE_ORIGINAL,
        SIGNETDEV_DEVICE_HC,
};

struct signetdev_phy_key {
	u8 scancode;
	u8 modifier;
};

struct signetdev_key {
	u16 key;
	struct signetdev_phy_key phy_key[2];
};

void signetdev_deinitialize_api();
enum signetdev_device_type signetdev_open_connection();
void signetdev_close_connection();

//Device query functions
int signetdev_max_entry_data_size();
int signetdev_device_block_size();
int signetdev_device_num_data_blocks();
int signetdev_device_num_root_blocks();
int signetdev_device_num_storage_blocks();
int signetdev_init_rand_data_size();

void signetdev_set_keymap(const struct signetdev_key *keys, int n_keys);
int signetdev_can_type(const u8 *keys, int n_keys);
int signetdev_can_type_w(const u16 *keys, int n_keys);
int signetdev_to_scancodes_w(const u16 *keys, int n_keys, u16 *out, int *out_len_);

const struct signetdev_key *signetdev_get_keymap(int *n_keys);

typedef void (*signetdev_conn_err_t)(void *);

void signetdev_set_error_handler(signetdev_conn_err_t handler, void *param);

typedef enum signetdev_cmd_id {
	SIGNETDEV_CMD_STARTUP,
	SIGNETDEV_CMD_LOGOUT,
	SIGNETDEV_CMD_LOGIN,
	SIGNETDEV_CMD_LOGIN_TOKEN,
	SIGNETDEV_CMD_WIPE,
	SIGNETDEV_CMD_BUTTON_WAIT,
	SIGNETDEV_CMD_DISCONNECT,
	SIGNETDEV_CMD_GET_PROGRESS,
	SIGNETDEV_CMD_BEGIN_DEVICE_BACKUP,
	SIGNETDEV_CMD_END_DEVICE_BACKUP,
	SIGNETDEV_CMD_BEGIN_DEVICE_RESTORE,
	SIGNETDEV_CMD_END_DEVICE_RESTORE,
	SIGNETDEV_CMD_BEGIN_UPDATE_FIRMWARE,
	SIGNETDEV_CMD_RESET_DEVICE,
        SIGNETDEV_CMD_SWITCH_BOOT_MODE,
	SIGNETDEV_CMD_TYPE,
	SIGNETDEV_CMD_CHANGE_MASTER_PASSWORD,
	SIGNETDEV_CMD_BEGIN_INITIALIZE_DEVICE,
	SIGNETDEV_CMD_READ_BLOCK,
	SIGNETDEV_CMD_WRITE_BLOCK,
	SIGNETDEV_CMD_WRITE_FLASH,
	SIGNETDEV_CMD_ERASE_PAGES,
	SIGNETDEV_CMD_ENTER_MOBILE_MODE,
	SIGNETDEV_CMD_UPDATE_UID,
	SIGNETDEV_CMD_UPDATE_UIDS,
	SIGNETDEV_CMD_READ_UID,
	SIGNETDEV_CMD_READ_ALL_UIDS,
	SIGNETDEV_CMD_GET_RAND_BITS,
	SIGNETDEV_CMD_GET_DEVICE_STATE,
	SIGNETDEV_CMD_READ_CLEARTEXT_PASSWORD,
	SIGNETDEV_CMD_READ_CLEARTEXT_PASSWORD_NAMES,
	SIGNETDEV_CMD_WRITE_CLEARTEXT_PASSWORD,
	SIGNETDEV_NUM_COMMANDS
} signetdev_cmd_id_t;

int signetdev_write_cleartext_password(void *param, int *token, int index, const struct cleartext_pass *data);
int signetdev_read_cleartext_password(void *param, int *token, int index);
int signetdev_read_cleartext_password_names(void *param, int *token);
int signetdev_enter_mobile_mode(void *user, int *token);
int signetdev_get_device_state(void *user, int *token);
int signetdev_logout(void *user, int *token);
int signetdev_login(void *user, int *token, u8 *key, unsigned int key_len, int gen_token);
int signetdev_login_token(void *user, int *api_token, u8 *login_token);
int signetdev_begin_update_firmware(void *user, int *token);
int signetdev_begin_update_firmware_hc(void *user, int *token, const struct hc_firmware_info *fw_info);
int signetdev_reset_device(void *user, int *token);
int signetdev_switch_boot_mode(void *user, int *token);
int signetdev_get_progress(void *user, int *token, int progress, int state);
int signetdev_wipe(void *user, int *token);
int signetdev_begin_device_backup(void *user, int *token);
int signetdev_end_device_backup(void *user, int *token);
int signetdev_begin_device_restore(void *user, int *token);
int signetdev_end_device_restore(void *user, int *token);
int signetdev_startup(void *param, int *token);
int signetdev_type(void *param, int *token, const u8 *keys, int n_keys);
int signetdev_type_w(void *param, int *token, const u16 *keys, int n_keys);
int signetdev_type_raw(void *param, int *token, const u8 *codes, int n_keys);
int signetdev_delete_id(void *param, int *token, int id);
int signetdev_button_wait(void *user, int *token);
int signetdev_change_master_password(void *param, int *token,
                                                u8 *old_key, u32 old_key_len,
                                                u8 *new_key, u32 new_key_len,
                                                u8 *hashfn, u32 hashfn_len,
                                                u8 *salt, u32 salt_len);
int signetdev_begin_initialize_device(void *param, int *token,
                                        const u8 *key, int key_len,
                                        const u8 *hashfn, int hashfn_len,
                                        const u8 *salt, int salt_len,
                                        const u8 *rand_data, int rand_data_len);
int signetdev_disconnect(void *user, int *token);
int signetdev_read_block(void *param, int *token, unsigned int idx);
int signetdev_write_block(void *param, int *token, unsigned int idx, const void *buffer);
int signetdev_get_rand_bits(void *param, int *token, int sz);
int signetdev_write_flash(void *param, int *token, u32 addr, const void *data, unsigned int data_len);
int signetdev_erase_pages(void *param, int *token, unsigned int n_pages, const u8 *page_numbers);
int signetdev_erase_pages_hc(void *param, int *token);

int signetdev_update_uid(void *user, int *token, unsigned int id, unsigned int size, const u8 *data, const u8 *mask);
int signetdev_update_uids(void *user, int *token, unsigned int id, unsigned int size, const u8 *data, const u8 *mask, unsigned int entries_remaining);
int signetdev_read_uid(void *param, int *token, int uid, int masked);
int signetdev_read_all_uids(void *param, int *token, int masked);
int signetdev_has_keyboard();

struct signetdev_get_rand_bits_resp_data {
	int size;
	const u8 *data;
};

struct signetdev_read_all_uids_resp_data {
	int uid;
	int size;
	u8 data[CMD_PACKET_PAYLOAD_SIZE];
	u8 mask[CMD_PACKET_PAYLOAD_SIZE];
};

struct signetdev_read_uid_resp_data {
	int size;
	u8 data[CMD_PACKET_PAYLOAD_SIZE];
	u8 mask[CMD_PACKET_PAYLOAD_SIZE];
};

struct signetdev_startup_resp_data {
	int fw_major_version;
	int fw_minor_version;
	int fw_step_version;
	int device_state;
	int root_block_format;
	int db_format;
        enum hc_boot_mode boot_mode;
        u32 upgrade_state;
	u8 hashfn[HASH_FN_SZ];
	u8 salt[SALT_SZ_V2];
};

struct signetdev_get_progress_resp_data {
	int n_components;
	int total_progress;
	int total_progress_maximum;
	int progress[8];
	int progress_maximum[8];
};

int signetdev_cancel_button_wait();


#ifdef _WIN32
#include <windows.h>
void signetdev_win32_set_window_handle(HANDLE recp);
int signetdev_filter_window_messasage(UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif

typedef void (*signetdev_cmd_resp_t)(void *cb_param, void *cmd_user_param, int cmd_token, int end_device_state, int messages_remaining, int cmd, int resp_code, const void *resp_data);
typedef void (*signetdev_device_event_t)(void *cb_param, int event_type, const void *resp_data, int resp_len);


void signetdev_set_device_opened_cb(void (*device_opened)(enum signetdev_device_type, void *), void *param);
void signetdev_set_device_closed_cb(void (*device_closed)(void *), void *param);
void signetdev_set_command_resp_cb(signetdev_cmd_resp_t cmd_resp_cb, void *cb_param);
void signetdev_set_device_event_cb(signetdev_device_event_t device_event_cb, void *cb_param);

int signetdev_emulate_init(const char *filename);
int signetdev_emulate_begin();
void signetdev_emulate_end();
void signetdev_emulate_deinit();

#endif


