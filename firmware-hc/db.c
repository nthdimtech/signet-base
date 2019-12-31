#include <memory.h>
#include "signetdev_common.h"
#include "types.h"
#include "crc.h"

//#include "flash.h"

#include "commands.h"
#include "signet_aes.h"
#include "main.h"

enum update_uid_status {
	UPDATE_UID_SUCCESS,
	UPDATE_UID_NO_SPACE,
	UPDATE_UID_INVALID,
	UPDATE_UID_DATA_LOADING
};

extern struct root_page _crypt_data1;
extern struct root_page _crypt_data2;

static u8 block_read_cache[BLK_SIZE];
static u8 block_read_cache_idx = -1;
static int block_read_cache_updating = 0;

static void update_uid_cmd_iter();
static void read_uid_cmd_iter();
static void db3_startup_scan_resume();

int db3_read_block_complete()
{
	if (block_read_cache_updating) {
		block_read_cache_updating = 0;
		switch(active_cmd) {
		case UPDATE_UID:
		case UPDATE_UIDS:
			update_uid_cmd_iter();
			return 1;
		case READ_UID:
			read_uid_cmd_iter();
			return 1;
		case READ_ALL_UIDS:
			read_all_uids_cmd_iter();
			return 1;
		}
	}
	switch (g_device_state) {
	case DS_INITIALIZING:
		db3_startup_scan_resume();
		return 1;
	default:
		break;
	}
	switch (active_cmd) {
	case STARTUP:
		db3_startup_scan_resume();
		return 1;
	default:
		break;
	}
	return 0;
}

int db3_write_block_complete()
{
	switch (active_cmd) {
	case UPDATE_UIDS:
	case UPDATE_UID:
		update_uid_cmd_write_finished();
		return 1;
	default:
		break;
	}
	return 0;
}

//NEN_TODO: Keep cache up to date when writes occur
// Should we invalidate the cache or update it?
static const u8 *get_cached_data_block(int idx)
{
	if (block_read_cache_idx == idx) {
		return block_read_cache;
	} else {
		block_read_cache_idx = idx;
		block_read_cache_updating = 1;
		read_data_block(idx, block_read_cache);
		return NULL;
	}
}

//NEN_TODO: need to select data source based on validation algorithm
#define _root_page _crypt_data1

extern u8 encrypt_key[AES_256_KEY_SIZE];

struct block_info g_block_info_tbl[MAX_DATA_BLOCK + 1];

static u8 uid_map[MAX_UID + 1]; //0 == invalid, block #

struct uid_ent {
	unsigned int uid : 12;
	unsigned int rev : 2;
	unsigned int first : 1;
	unsigned int pad : 1;
	u16 sz;
	u16 blk_next;
} __attribute__((__packed__));

struct block_header {
	u32 crc;
	u16 part_size;
	u16 occupancy;
} __attribute__((__packed__));

struct block {
	struct block_header header;
	struct uid_ent uid_tbl[];
} __attribute__((__packed__));

int db3_startup_scan_running = 0;
static int db3_startup_scan_blk_num = -1;
static struct block *db3_startup_scan_block_read;
static struct block_info *db3_startup_scan_blk_info_temp;

static enum update_uid_status find_uid (int uid, const struct uid_ent **, int *block_num, struct block **block, int *index);
static enum update_uid_status deallocate_uid (int uid, int *block_num, struct block *block_temp, struct block_info *blk_info_temp, int deallocate_block);
static int block_crc(const struct block *blk)
{
	return crc_32((&blk->header.crc) + 1, (BLK_SIZE/4) - 1);
}

//
// Returns non-zero if CRC and partition size are in an invalid state OR the CRC is
// correct. If the CRC is correct then the block is allocated otherwise th
//
static int block_crc_check(const struct block *blk)
{
	if (blk->header.part_size == INVALID_PART_SIZE) {
		return 0;
	}
	u32 res = block_crc(blk);
	return (res == blk->header.crc) ? 1 : 0;
}

static int get_block_header_size(int part_count)
{
	return SUB_BLK_COUNT(sizeof(struct block_header) + (sizeof(struct uid_ent) * part_count));
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

//Returns a pointer to the n'th partition in 'block'
static u8 *get_part(const struct block *block, const struct block_info *info, int n)
{
	return ((u8 *)block) + ((info->part_tbl_offs + (info->part_size * n)) * SUB_BLK_SIZE);
}

static void db3_startup_scan_resume ()
{
	int i = db3_startup_scan_blk_num;
	struct block *block_read = db3_startup_scan_block_read;
#if 0
	struct block_info *blk_info_temp = db3_startup_scan_blk_info_temp;
#endif

	struct block_info *blk_info = g_block_info_tbl + i;
	blk_info->part_size = block_read->header.part_size;
	blk_info->valid = block_crc_check(block_read) || blk_info->part_size == INVALID_PART_SIZE;
	blk_info->occupied = (blk_info->part_size != INVALID_PART_SIZE);
	if (!blk_info->valid) {
		//NEN_TODO: What do we do here?
	}
	if (blk_info->occupied) {
		blk_info->part_occupancy = block_read->header.occupancy;
		blk_info->part_count = get_part_count(block_read->header.part_size);
		blk_info->part_tbl_offs = get_block_header_size(blk_info->part_count);
		for (int j = 0; j < blk_info->part_occupancy; j++) {
			const struct uid_ent *ent = block_read->uid_tbl + j;
			int uid = ent->uid;
			if (uid >= MIN_UID && ent->uid <= MAX_UID && ent->first) {
#if 0
//NEN_TODO: This code needs review and testing. Looks probably incorrect
				if (uid_map[uid] != INVALID_BLOCK) {
					int block_num_temp;
					int index_temp;
					const struct uid_ent *prev_ent;
					struct block *blk;
				       	enum update_uid_status rc = find_uid(uid, &prev_ent, &block_num_temp, &blk, &index_temp);
					if (rc != UPDATE_UID_SUCCESS) {
						return;
					}
					if (((ent->rev + 1) & 0x3) == prev_ent->rev) {
						//NEN_TODO: sould be setting block num here
						uid_map[uid] = i;
					}
					struct block *block = (struct block *)block_temp;
					deallocate_uid(uid, block_num_temp, block, blk_info_temp, 1 /* dellocate block */);
					if (blk_info_temp->occupied) {
						block->header.crc = block_crc(block);
					}
					write_data_block(block_num, (u8 *)block, BLK_SIZE);
					return 0;
				} else {
					uid_map[uid] = i;
				}
#else
				uid_map[uid] = i;
#endif
			}
		}
	}
	db3_startup_scan_blk_num++;
	if (db3_startup_scan_blk_num <= MAX_DATA_BLOCK) {
		read_data_block(db3_startup_scan_blk_num, (u8 *)block_read);
	} else {
		db3_startup_scan_running = 0;
		//NEN_TODO: this functionality should be in callbacks
		if (active_cmd == STARTUP) {
			enter_state(DS_LOGGED_OUT);
			cmd_data.startup.resp[3] = g_device_state;
			finish_command(OKAY, cmd_data.startup.resp, sizeof(cmd_data.startup.resp));
		} else if (g_device_state == DS_INITIALIZING) {
			enter_state(DS_LOGGED_OUT);
		}
	}
}

//Scans blocks on startup to initialize 'g_block_info_tbl' and 'uid_map'
void db3_startup_scan (u8 *block_read, struct block_info *blk_info_temp)
{
	int i;
	for (i = MIN_UID; i <= MAX_UID; i++) {
		uid_map[i] = INVALID_BLOCK;
	}
	db3_startup_scan_running = 1;
	db3_startup_scan_blk_info_temp = blk_info_temp;
	db3_startup_scan_block_read = (struct block *)block_read;
	db3_startup_scan_blk_num = MIN_DATA_BLOCK;
	read_data_block(db3_startup_scan_blk_num, (u8 *)db3_startup_scan_block_read);
}

#define MAX_PART_SIZE (BLK_SIZE - sizeof(struct block) - sizeof(struct uid_ent))/SUB_BLK_SIZE;

//Return a block that has not been allocated or INVALID_BLOCK if there are no free blocks
static int find_free_block()
{
	for (int i = MIN_DATA_BLOCK; i <= MAX_DATA_BLOCK; i++) {
		struct block_info *blk_info = g_block_info_tbl + i;
		if (blk_info->valid && !blk_info->occupied) {
			return i;
		}
	}
	return INVALID_BLOCK;
}

static void initialize_block(int part_size, struct block *block)
{
	memset(block, 0, BLK_SIZE);
	block->header.part_size = part_size;
	block->header.occupancy = 0;
}

struct block *db3_initialize_block(int block_num, struct block *block)
{
	int part_size = MAX_PART_SIZE;
	initialize_block(part_size, block);
	block->header.crc = block_crc(block);
	return block;
}

static void allocate_uid_blk(int uid, const u8 *data, int sz, int rev, const u8 *iv, struct block *block_temp, struct block_info *blk_info_temp)
{
	int index = blk_info_temp->part_occupancy;
	blk_info_temp->part_occupancy++;
	blk_info_temp->occupied = 1;
	blk_info_temp->valid = 1;
	block_temp->header.occupancy++;
	block_temp->uid_tbl[index].uid = uid;
	block_temp->uid_tbl[index].sz = sz;
	block_temp->uid_tbl[index].rev = rev;
	block_temp->uid_tbl[index].first = 1;
	block_temp->uid_tbl[index].blk_next = INVALID_BLOCK;

	int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);

	signet_aes_256_encrypt_cbc(encrypt_key, blk_count, iv, data, get_part(block_temp, blk_info_temp, index));
}

static enum update_uid_status allocate_uid (int uid, const u8 *data, int sz, int rev, const u8 *iv, int *block_num, struct block *block_temp, struct block_info *blk_info_temp)
{
	int part_size = MAX_PART_SIZE;
	*block_num = INVALID_BLOCK;

	if (!part_size) {
		return UPDATE_UID_INVALID;
	}

	int allocating_block = 0;

	//Try to find a block with the right partition size
	for (int i = MIN_DATA_BLOCK; i <= MAX_DATA_BLOCK; i++) {
		struct block_info *blk_info = g_block_info_tbl + i;
		if (!blk_info->valid || !blk_info->occupied)
			continue;
		if (blk_info->part_size == part_size && blk_info->part_occupancy < blk_info->part_count) {
			*block_num = i;
			memcpy(blk_info_temp, g_block_info_tbl + *block_num, sizeof(*blk_info_temp));
			break;
		}
	}

	//If we can't try to create a new block with the right partition size
	if (*block_num == INVALID_BLOCK) {
		*block_num = find_free_block();
		if (*block_num != INVALID_BLOCK) {
			initialize_block(part_size, block_temp);
			blk_info_temp->part_occupancy = 0;
			blk_info_temp->part_size = part_size;
			blk_info_temp->part_count = get_part_count(blk_info_temp->part_size);
			blk_info_temp->part_tbl_offs = get_block_header_size(blk_info_temp->part_count);
			allocating_block = 1;
		}
	}

	if (*block_num == INVALID_BLOCK) {
		//Try to find a block with a large enough partition size if
		//an exact match or new block can't be found
		int found_part_size = BLK_SIZE;
		for (int i = MIN_DATA_BLOCK; i <= MAX_DATA_BLOCK; i++) {
			struct block_info *blk_info = g_block_info_tbl + i;
			if (!blk_info->valid || !blk_info->occupied)
				continue;
			if (blk_info->part_size >= part_size && blk_info->part_size < found_part_size && blk_info->part_occupancy < blk_info->part_count) {
				*block_num = i;
				found_part_size = blk_info->part_size;
				memcpy(blk_info_temp, g_block_info_tbl + *block_num, sizeof(*blk_info_temp));
			}
		}
	}

	if (*block_num != INVALID_BLOCK && !allocating_block) {
		const u8 *blk = get_cached_data_block(*block_num);
		if (!blk) {
			return UPDATE_UID_DATA_LOADING;
		}
		memcpy(block_temp, blk, BLK_SIZE);
	}

	if (block_num != INVALID_BLOCK) {
		allocate_uid_blk(uid, data, sz, rev, iv, block_temp, blk_info_temp);
		return UPDATE_UID_SUCCESS;
	} else {
		return UPDATE_UID_NO_SPACE;
	}
}

static enum update_uid_status find_uid (int uid, const struct uid_ent **ent, int *block_num, struct block **block, int *index)
{
	*block_num = uid_map[uid];
	if (*block_num == INVALID_BLOCK) {
		return UPDATE_UID_INVALID;
	}
	const u8 *blk = get_cached_data_block(*block_num);
	if (!blk) {
		return UPDATE_UID_DATA_LOADING;
	}
	if (block) {
		*block = (struct block *)blk;
	}
	struct block_info *blk_info = g_block_info_tbl + *block_num;
	*index = -1;
	for (int j = 0; j < blk_info->part_occupancy; j++) {
		*ent = (*block)->uid_tbl + j;
		if ((*ent)->uid == uid) {
			*index = j;
			return UPDATE_UID_SUCCESS;
		}
	}
	*ent = NULL;
	return UPDATE_UID_INVALID;
}

static enum update_uid_status deallocate_uid (int uid, int *block_num, struct block *block_temp, struct block_info *blk_info_temp, int deallocate_block)
{
	struct block *blk = NULL;
	int index;
	const struct uid_ent *ent = NULL;
	enum update_uid_status rc = find_uid(uid, &ent, block_num, &blk, &index);
	if (rc != UPDATE_UID_SUCCESS) {
		return rc;
	}
	if (!ent) {
		return UPDATE_UID_INVALID;
	}
	struct block_info *blk_info = g_block_info_tbl + *block_num;
	memcpy(blk_info_temp, blk_info, sizeof(*blk_info_temp));
	if (blk_info_temp->part_occupancy == 1 && deallocate_block) {
		//Free the block
		blk_info_temp->valid = 1;
		blk_info_temp->occupied = 0;
		block_temp->header.crc = INVALID_CRC;
		block_temp->header.part_size = INVALID_PART_SIZE;
		block_temp->header.occupancy = 0;
	} else {
		blk_info_temp->part_occupancy--;
		memcpy(block_temp, blk, BLK_SIZE);
		block_temp->header.occupancy--;
		if (block_temp->header.occupancy) {
			memcpy(get_part(block_temp, blk_info_temp, index),
			       get_part(block_temp, blk_info_temp, blk_info_temp->part_occupancy),
			       blk_info_temp->part_size * SUB_BLK_SIZE);
			memcpy(block_temp->uid_tbl + index,
			       block_temp->uid_tbl + blk_info_temp->part_occupancy,
			       sizeof(struct uid_ent));
		}
	}
	return UPDATE_UID_SUCCESS;
}

static enum update_uid_status update_uid (int uid, u8 *data, int sz,
		int *prev_block_num,
		int *next_block_num,
		const u8 *iv,
		struct block *block_temp,
		struct block_info *blk_info_temp)
{
	enum update_uid_status rc;
	if (!sz) {
		*prev_block_num = INVALID_BLOCK;
		return deallocate_uid(uid, next_block_num, block_temp, blk_info_temp, 1 /* dellocate block */);
	} else {
		int index;
		const struct uid_ent *ent;
		struct block *blk;
		rc = find_uid(uid, &ent, next_block_num, &blk, &index);
		switch (rc) {
		case UPDATE_UID_INVALID: {
			//Allocate for the first time
			*prev_block_num = INVALID_BLOCK;
			rc = allocate_uid(uid, data, sz, 0 /* rev */, iv, next_block_num, block_temp, blk_info_temp);
			if (rc != UPDATE_UID_SUCCESS) {
				return rc;
			}
		} break;
		case UPDATE_UID_SUCCESS: {
			int part_size = MAX_PART_SIZE;
			if (!part_size) {
				return UPDATE_UID_NO_SPACE;
			}
			struct block_info *blk_info = g_block_info_tbl + *next_block_num;
			memcpy(blk_info_temp, blk_info, sizeof(*blk_info_temp));
			if (blk_info_temp->part_size == part_size) {
				//Keep entry in single block by deallocating
				//and reallocating within the block
				memcpy(block_temp, blk, BLK_SIZE);
				*prev_block_num = *next_block_num;

				rc = deallocate_uid(uid, prev_block_num, block_temp, blk_info_temp, 0 /* deallocate block */);
				if (rc != UPDATE_UID_SUCCESS) {
					return rc;
				}

				allocate_uid_blk(uid, data, sz, ent->rev, iv, block_temp, blk_info_temp);
				return UPDATE_UID_SUCCESS;
			} else {
				//Move entry to new block first
				int new_rev = (ent->rev + 1) & 0x3;
				*prev_block_num = *next_block_num;
				rc = allocate_uid(uid, data, sz, new_rev, iv, next_block_num, block_temp, blk_info_temp);
				if (rc != UPDATE_UID_SUCCESS) {
					return rc;
				}
			}
		} break;
		default:
			return rc;
			break;
		}
	}
	return UPDATE_UID_SUCCESS;
}

void update_uid_cmd(int uid, u8 *data, int sz, int press_type)
{
	derive_iv(uid, cmd_data.update_uid.iv);
	cmd_data.update_uid.uid = uid;
	cmd_data.update_uid.sz = sz;
	cmd_data.update_uid.write_count = 0;
	cmd_data.update_uid.press_type = press_type;
	cmd_data.update_uid.prev_block_num = INVALID_BLOCK;
	memcpy(cmd_data.update_uid.entry, data, sz);
	cmd_data.update_uid.entry_sz = sz;
	update_uid_cmd_iter();
}

static void update_uid_cmd_iter()
{
	enum update_uid_status rc = update_uid(cmd_data.update_uid.uid,
	                                cmd_data.update_uid.entry,
	                                cmd_data.update_uid.entry_sz,
	                                &cmd_data.update_uid.prev_block_num,
	                                &cmd_data.update_uid.block_num,
	                                cmd_data.update_uid.iv,
	                                (struct block *)cmd_data.update_uid.block,
	                                &cmd_data.update_uid.blk_info);
	switch (rc) {
	case UPDATE_UID_SUCCESS:
		if (cmd_data.update_uid.press_type == 0) {
			update_uid_cmd_complete();
		} else if (cmd_data.update_uid.press_type == 1) {
			begin_button_press_wait();
		} else {
			begin_long_button_press_wait();
		}
		break;
	case UPDATE_UID_NO_SPACE:
		finish_command_resp(NOT_ENOUGH_SPACE);
		break;
	case UPDATE_UID_DATA_LOADING:
		break;
	default:
		break;
	}
}

void update_uid_cmd_complete()
{
	struct block *block = (struct block *)cmd_data.update_uid.block;
	if (cmd_data.update_uid.blk_info.occupied) {
		block->header.crc = block_crc(block);
	}
	write_data_block(cmd_data.update_uid.block_num, (u8 *)block);
}

void update_uid_cmd_write_finished()
{
	cmd_data.update_uid.write_count++;
	if (cmd_data.update_uid.write_count == 1) {
		//This is the first write completed
		memcpy(g_block_info_tbl + cmd_data.update_uid.block_num, &cmd_data.update_uid.blk_info, sizeof(struct block_info));
		if (cmd_data.update_uid.prev_block_num != cmd_data.update_uid.block_num && cmd_data.update_uid.prev_block_num != INVALID_BLOCK) {
			//Record has moved to a new block. Need to dellocate from original block now
			struct block *block = (struct block *)cmd_data.update_uid.block;
			deallocate_uid(cmd_data.update_uid.uid, &cmd_data.update_uid.prev_block_num, block, &cmd_data.update_uid.blk_info, 1 /* dellocate block*/);
			block->header.crc = block_crc(block);
			write_data_block(cmd_data.update_uid.prev_block_num, (u8 *)block);
		} else {
			if (!cmd_data.update_uid.sz) {
				//Record is being deleted
				uid_map[cmd_data.update_uid.uid] = INVALID_BLOCK;
			} else {
				//Record is staying in the same block or has been added for the first time
				uid_map[cmd_data.update_uid.uid] = cmd_data.update_uid.block_num;
			}
			finish_command_resp(OKAY);
		}
	} else {
		memcpy(g_block_info_tbl + cmd_data.update_uid.prev_block_num, &cmd_data.update_uid.blk_info, sizeof(struct block_info));
		uid_map[cmd_data.update_uid.uid] = cmd_data.update_uid.block_num;
		finish_command_resp(OKAY);
	}
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

static int decode_uid(int sz, int block_num, const struct block *blk, int index, int masked, const u8 *iv, u8 *dest)
{
	struct block_info *blk_info = g_block_info_tbl + block_num;
	int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);
	signet_aes_256_decrypt_cbc(encrypt_key, blk_count, iv, get_part(blk, blk_info, index), dest);
	if (masked) {
		mask_uid_data(dest, blk_count);
	}
	return blk_count;
}

void read_uid_cmd(int uid, int masked)
{
	cmd_data.read_uid.uid = uid;
	cmd_data.read_uid.masked = masked;
	cmd_data.read_uid.waiting_for_button_press = 0;
	derive_iv(cmd_data.read_uid.uid, cmd_data.read_uid.iv);
	read_uid_cmd_iter();
}

static void read_uid_cmd_iter()
{
	struct block *blk;
	enum update_uid_status rc = find_uid(cmd_data.read_uid.uid, &cmd_data.read_uid.ent, &cmd_data.read_uid.block_num, &blk, &cmd_data.read_uid.index);
	switch (rc) {
	case UPDATE_UID_INVALID:
		finish_command_resp(ID_INVALID);
		return;
	case UPDATE_UID_SUCCESS:
		if (!cmd_data.read_uid.masked) {
			cmd_data.read_uid.waiting_for_button_press = 1;
		} else {
			cmd_data.read_uid.waiting_for_button_press = 0;
		}
		break;
	default:
		return;
	}

	if (!cmd_data.read_uid.ent) {
		finish_command_resp(ID_INVALID);
		return;
	}

	if (!cmd_data.read_uid.masked && cmd_data.read_uid.waiting_for_button_press) {
		begin_button_press_wait();
	} else {
		read_uid_cmd_complete();
	}
}

void read_uid_cmd_complete()
{
	cmd_data.read_uid.waiting_for_button_press = 0;
	u8 *block = cmd_data.read_uid.block;
	block[0] = cmd_data.read_uid.ent->sz & 0xff;
	block[1] = cmd_data.read_uid.ent->sz >> 8;
	const u8 *blk = get_cached_data_block(cmd_data.read_uid.block_num);
	if (!blk) {
		return;
	}
	int blk_count = decode_uid(cmd_data.read_uid.ent->sz, cmd_data.read_uid.block_num, (struct block *)blk, cmd_data.read_uid.index, cmd_data.read_uid.masked, cmd_data.read_uid.iv, block + 2);
	finish_command(OKAY, block, (blk_count * SUB_BLK_SIZE) + 2);
}

void read_all_uids_cmd_iter()
{
	u8 *block = cmd_data.read_all_uids.block;
	int block_num;
	const struct uid_ent *ent;
	int index;
	struct block *blk;
	do {
		while (uid_map[cmd_data.read_all_uids.uid] == INVALID_BLOCK && cmd_data.read_all_uids.uid <= MAX_UID)
			cmd_data.read_all_uids.uid++;

		if (cmd_data.read_all_uids.uid > MAX_UID) {
			block[0] = cmd_data.read_all_uids.uid & 0xff;
			block[1] = cmd_data.read_all_uids.uid >> 8;
			finish_command_multi(ID_INVALID, 0, block, 2);
			return;
		}
		enum update_uid_status rc = find_uid(cmd_data.read_all_uids.uid, &ent, &block_num, &blk, &index);
		if (rc != UPDATE_UID_SUCCESS) {
			return;
		}
		cmd_data.read_all_uids.expected_remaining--;
	} while(!ent);

	block[0] = cmd_data.read_all_uids.uid & 0xff;
	block[1] = cmd_data.read_all_uids.uid >> 8;
	block[2] = ent->sz & 0xff;
	block[3] = ent->sz >> 8;
	derive_iv(cmd_data.read_all_uids.uid, cmd_data.read_all_uids.iv);
	int blk_count = decode_uid(ent->sz, block_num, blk, index, cmd_data.read_all_uids.masked, cmd_data.read_all_uids.iv, cmd_data.read_all_uids.block + 4);
	finish_command_multi(OKAY, cmd_data.read_all_uids.expected_remaining, block, (blk_count * SUB_BLK_SIZE) + 4);
}

void read_all_uids_cmd_complete()
{
	cmd_data.read_all_uids.uid++;
	read_all_uids_cmd_iter();
}

void read_all_uids_cmd(int masked)
{
	cmd_data.read_all_uids.uid = 0;
	cmd_data.read_all_uids.masked = masked;
	cmd_data.read_all_uids.expected_remaining = 0;

	for (int i = MIN_UID; i <= MAX_UID; i++)
		if(uid_map[i] != INVALID_BLOCK)
			cmd_data.read_all_uids.expected_remaining++;

	if (!masked) {
		begin_long_button_press_wait();
	} else {
		read_all_uids_cmd_iter();
	}
}
