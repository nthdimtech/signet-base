#ifndef COMMANDS_H
#define COMMANDS_H
#include "types.h"
#include "signetdev_common.h"
#include "db.h"

void get_progress_cmd(u8 *data, int data_len);

void finish_command(enum command_responses resp, const u8 *payload, int payload_len);
void finish_command_resp(enum command_responses resp);
void finish_command_multi(enum command_responses resp, int messages_remaining, const u8 *payload, int payload_len);
void derive_iv(u32 id, u8 *iv);
void begin_button_press_wait();
void begin_long_button_press_wait();
void invalidate_data_block_cache(int idx);
void get_progress_check();

extern enum device_state g_device_state;
extern int active_cmd;

union state_data_u {
	struct {
		int prev_state;
	} backup;
};

union cmd_data_u {
	struct {
		u16 index;
		u16 min_page;
		u16 max_page;
	} erase_flash_pages;
	struct {
		u8 chars[CMD_PACKET_PAYLOAD_SIZE];
	} type_data;
	struct {
		int started;
		int rand_avail_init;
		int random_data_gathered;
		int root_block_finalized;
		int blocks_written;
		u8 passwd[AES_256_KEY_SIZE];
		u8 hashfn[AES_BLK_SIZE];
		u8 salt[AES_256_KEY_SIZE];
		u8 rand[INIT_RAND_DATA_SZ];
		u8 block[BLK_SIZE];
		struct block_info blk_info;
	} init_data;
	struct {
		u8 read_block[BLK_SIZE];
		u8 block[BLK_SIZE];
		u8 resp[STARTUP_RESP_SIZE];
		struct block_info blk_info;
	} startup;
	struct {
		u8 block[BLK_SIZE];
		int block_idx;
	} wipe_data;
	struct {
		u8 block[BLK_SIZE];
		int block_idx;
	} read_block;
	struct {
		u8 block[BLK_SIZE];
		int block_idx;
	} write_block;
	struct {
		u8 block[BLK_SIZE];
		int block_idx;
	} erase_block;
	struct {
		u8 new_key[AES_256_KEY_SIZE];
		u8 keystore_key[AES_256_KEY_SIZE];
		u8 cyphertext[AES_256_KEY_SIZE];
		u8 hashfn[AES_BLK_SIZE];
		u8 salt[AES_256_KEY_SIZE];
	} change_master_password;
	struct {
		u8 password[AES_256_KEY_SIZE];
		u8 cyphertext[AES_256_KEY_SIZE];
		int authenticated;
		int gen_token;
		u32 token[AES_256_KEY_SIZE/4];
	} login;
	struct {
		u32 token[AES_256_KEY_SIZE/4];
	} login_token;

	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int sz;
		int write_count;
		int block_num;
		int prev_block_num;
		int press_type;
		struct block_info blk_info;
		u8 entry[BLK_SIZE];
		int entry_sz;
		u8 block[BLK_SIZE];
		int update_uid_stage;
	} update_uid;
	struct {
		u8 block[NUM_CLEARTEXT_PASS * 64];
	} read_cleartext_password_names;
	struct {
		u8 block[BLK_SIZE];
		u32 addr;
		u16 sz;
	} write_flash;
	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int block_num;
		int index;
		const struct uid_ent *ent;
		int masked;
		int waiting_for_button_press;
		u8 block[BLK_SIZE];
	} read_uid;
	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int expected_remaining;
		int masked;
		u8 block[BLK_SIZE];
	} read_all_uids;
	struct {
		u16 sz;
		u8 block[BLK_SIZE];
	} get_rand_bits;
	struct {
		int idx;
		u8 data[CLEARTEXT_PASS_SIZE];
	} write_cleartext_password;
	struct {
		int idx;
	} read_cleartext_password;
} __attribute__((aligned(16)));

extern union cmd_data_u cmd_data;

int cmd_packet_recv();
void cmd_init();
void cmd_packet_send(const u8 *data, u16 len);
void cmd_event_send(int event_num, const u8 *data, int data_len);

extern u8 cmd_packet_buf[];

void enter_state(enum device_state state);
void enter_progressing_state(enum device_state state, int _n_progress_components, int *_progress_maximums);

void cmd_rand_update();
void write_data_block(int pg, const u8 *src);
void read_data_block(int pg, u8 *dest);

enum emmc_user {
	EMMC_USER_NONE,
	EMMC_USER_STORAGE,
	EMMC_USER_DB,
	EMMC_NUM_USER
};

void emmc_user_queue(enum emmc_user user);
void emmc_user_schedule();
void emmc_user_done();

//TODO: Use functions to update progress
extern int g_progress_level[];

void command_idle();

#endif
