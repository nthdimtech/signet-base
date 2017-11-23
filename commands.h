#ifndef COMMANDS_H
#define COMMANDS_H
#include "types.h"
#include "signetdev/common/signetdev_common.h"
#include "db.h"

void get_progress_cmd(u8 *data, int data_len);

void finish_command(enum command_responses resp, const u8 *payload, int payload_len);
void finish_command_resp(enum command_responses resp);
void finish_command_multi(enum command_responses resp, int messages_remaining, const u8 *payload, int payload_len);
void derive_iv(u32 id, u8 *iv);
void begin_button_press_wait();
void begin_long_button_press_wait();
extern u8 cmd_resp[];

extern enum device_state device_state;

union state_data_u {
	struct {
		int prev_state;
	} backup;
};

union cmd_data_u {
	struct {
		u16 index;
		u16 num_pages;
		u8 pages[CMD_PACKET_PAYLOAD_SIZE];
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
	} init_data;
	struct {
		int block;
	} wipe_data;
	struct {
		u8 new_key[AES_256_KEY_SIZE];
		u8 cyphertext[AES_256_KEY_SIZE];
		u8 hashfn[AES_BLK_SIZE];
		u8 salt[AES_256_KEY_SIZE];
	} change_master_password;
	struct {
		u8 password[AES_256_KEY_SIZE];
		u8 cyphertext[AES_256_KEY_SIZE];
	} login;

	//V1 commands
	struct {
		u8 iv[AES_BLK_SIZE];
		u8 block[BLK_SIZE];
		int id;
		int sub_blk_count;

	} set_data;
	struct {
		int id;
		int sz;
		u8 iv[AES_BLK_SIZE];
		u8 block[BLK_SIZE];
	} get_data;
	struct {
		int id;
		int unmask;
		u8 iv[AES_BLK_SIZE];
		u8 block[BLK_SIZE];
	} get_all_data;
	struct {
		int id;
	} delete_id;

	//V2 commands
	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int sz;
		int write_count;
		int block_num;
		int prev_block_num;
		struct block_info blk_info;
		u8 block[BLK_SIZE];
	} update_uid;
	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int block_num;
		int index;
		const struct uid_ent *ent;
		int masked;
		u8 block[BLK_SIZE];
	} read_uid;
	struct {
		u8 iv[AES_BLK_SIZE];
		int uid;
		int expected_remaining;
		int masked;
		u8 block[BLK_SIZE];
	} read_all_uids;
};

extern union cmd_data_u cmd_data;

int cmd_packet_recv();
void cmd_packet_send(const u8 *data, u16 len);
void cmd_event_send(int event_num, const u8 *data, int data_len);

extern u8 cmd_packet_buf[];

void enter_state(enum device_state state);
void enter_progressing_state(enum device_state state, int _n_progress_components, int *_progress_maximums);

void cmd_rand_update();

#endif
