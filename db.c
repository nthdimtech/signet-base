#include "common.h"
#include "types.h"
#include "crc.h"
#include "mem.h"
#include "flash.h"
#include "commands.h"
#include "stm_aes.h"
#include "main.h"

extern struct root_page _root_page;

#define BLOCK(id) ((const struct block *)(((const u8 *)&_root_page) + BLK_SIZE * id))

#define MAX_UID (1<<12)
#define MIN_UID (1)
#define INVALID_UID (0)
#define INVALID_BLOCK (0)
#define MIN_BLOCK (1)
#define MAX_BLOCK (NUM_STORAGE_BLOCKS-1)
#define INVALID_PART_SIZE (0xff)
#define INVALID_CRC (0xffff)

extern u8 encrypt_key[AES_256_KEY_SIZE];

struct block_info block_info_tbl[NUM_STORAGE_BLOCKS];


static u8 uid_map[MAX_UID]; //0 == invalid, block #

#define NUM_PART_SIZES 11

static int part_sizes[NUM_PART_SIZES] = {1,2,3,4,6,7,12,15,31,63,127};

struct uid_ent {
	unsigned int uid : 12;
	unsigned int rev : 2;
	unsigned int first : 1;
	unsigned int pad : 1;
	u16 sz;
	u8 blk_next;
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


static int block_crc(const struct block *blk)
{
	return crc_32((&blk->header.crc) + 1, (BLK_SIZE/4) - 1);
}

static int block_crc_check(const struct block *blk)
{
	if (blk->header.part_size == INVALID_PART_SIZE && blk->header.crc == INVALID_CRC) {
		return 1;
	}
	u32 res = block_crc(blk);
	return res == blk->header.crc;
}

static int get_block_header_size(int part_count)
{
	return SIZE_TO_SUB_BLK_COUNT(sizeof(struct block_header) + (sizeof(struct uid_ent) * part_count));
}

static int get_part_count(int part_size)
{
	int max_sub_blocks = (BLK_SIZE/SUB_BLK_SIZE);
	int count = max_sub_blocks/part_size;
	while (count && ((get_block_header_size(count) + (count * part_size))) > max_sub_blocks) {
		count--;
	}
	return count;
}

static u8 *get_part(struct block *block, const struct block_info *info, int index)
{
	return ((u8 *)block) + ((info->part_tbl_offs + (info->part_size * index)) * SUB_BLK_SIZE);
}

void db2_startup_scan()
{
	for (int i = MIN_BLOCK; i <= MAX_BLOCK; i++) {
		const struct block *blk = BLOCK(i);
		struct block_info *blk_info = block_info_tbl + i;
		if (!block_crc_check(blk)) {
			blk_info->valid = 0;
			continue;
		}
		
		blk_info->occupied = (blk_info->part_size != INVALID_PART_SIZE);
		if (blk_info->occupied) {
			//TODO: more validity checks
			blk_info->part_size = blk->header.part_size;
			blk_info->part_occupancy = blk->header.occupancy;
			blk_info->part_count = get_part_count(blk->header.part_size);
			blk_info->part_tbl_offs = get_block_header_size(blk_info->part_count);
		}
		if (blk_info->occupied) {
			for (int j = 0; j < blk_info->part_occupancy; j++) {
				const struct uid_ent *ent = blk->uid_tbl + j;
				int uid = ent->uid;
				if (uid >= MIN_UID && ent->uid <= MAX_UID && ent->first) {
					//TODO: check if already populated. Should prefer later revisions
					uid_map[ent->uid] = i;
				}
			}
		}
	}
}

static int target_part_size(int data_bytes)
{
	return 63; //TODO: decide this dynamically
}

static int find_free_block()
{
	for (int i = MIN_BLOCK; i <= MAX_BLOCK; i++) {
		struct block_info *blk_info = block_info_tbl + i;
		if (blk_info->valid && !blk_info->occupied) {
			return i;
		}
	}
	return INVALID_BLOCK;
}

static void allocate_uid_blk(int uid, const u8 *data, int sz, int rev, const u8 *iv, struct block *block_temp, struct block_info *blk_info_temp)
{
	int index = blk_info_temp->part_occupancy;
	blk_info_temp->part_occupancy++;
	block_temp->header.occupancy++;
	block_temp->uid_tbl[index].uid = uid;
	block_temp->uid_tbl[index].sz = sz;
	block_temp->uid_tbl[index].rev = rev;
	block_temp->uid_tbl[index].blk_next = INVALID_BLOCK;
	
	int blk_count = SIZE_TO_SUB_BLK_COUNT(sz);

	//TODO: data needs to be SUB_BLK_SIZE padded...	
	stm_aes_256_encrypt_cbc(encrypt_key, blk_count, iv, data, get_part(block_temp, blk_info_temp, index));
	
	block_temp->header.crc = block_crc(block_temp);
}

static int allocate_uid(int uid, const u8 *data, int sz, int rev, const u8 *iv, struct block *block_temp, struct block_info *blk_info_temp)
{
	//TODO return error if there is not space for one extra block of the same size
	// so errors never occur when 
	int part_size = target_part_size(sz);
	int block_num = INVALID_BLOCK;

	//Try to find a block with the right partition size
	for (int i = MIN_BLOCK; i <= MAX_BLOCK; i++) {
		struct block_info *blk_info = block_info_tbl + i;
		if (!blk_info->valid || !blk_info->occupied)
			continue;
		if (blk_info->part_size == part_size && blk_info->part_occupancy < blk_info->part_count) {
			block_num = i;
			memcpy(blk_info_temp, block_info_tbl + block_num, sizeof(*blk_info_temp));
			break;
		}
	}

	//If we can't try to create a new block with the right partition size
	if (block_num == INVALID_BLOCK) {
		block_num = find_free_block();
		struct block_info *blk_info = block_info_tbl + block_num;
		memset(block_temp, 0, BLK_SIZE);
		block_temp->header.occupancy = 0;
		block_temp->header.part_size = target_part_size(sz); 
		blk_info_temp->part_size = target_part_size(sz);
		blk_info_temp->part_occupancy = 0;
		blk_info_temp->part_count = get_part_count(blk_info_temp->part_size);
		blk_info_temp->part_tbl_offs = get_block_header_size(blk_info->part_count);
	}

	if (block_num != INVALID_BLOCK) {
		const struct block *blk = BLOCK(block_num);
		memcpy(block_temp, blk, BLK_SIZE);
		allocate_uid_blk(uid, data, sz, rev, iv, block_temp, blk_info_temp);
		return block_num;
	}
	return INVALID_BLOCK;
}

static const struct uid_ent *find_uid(int uid, int *block_num, int *index)
{
	*block_num = uid_map[uid];
	if (*block_num == INVALID_BLOCK) {
		return NULL;
	}
	const struct block *blk = BLOCK(*block_num);
	struct block_info *blk_info = block_info_tbl + *block_num;
	*index = -1;
	for (int j = 0; j < blk_info->part_occupancy; j++) {
		const struct uid_ent *ent = blk->uid_tbl + j;
		int uid = ent->uid;
		if (ent->uid == uid) {
			*index = j;
			return ent;
		}
	}
	return NULL;
}

static int deallocate_uid(int uid, struct block *block_temp, struct block_info *blk_info_temp)
{
	int block_num;
        int index;
	const struct uid_ent *ent = find_uid(uid, &block_num, &index);
	if (!ent) {
		return INVALID_BLOCK;
	}
	struct block_info *blk_info = block_info_tbl + block_num;
	memcpy(blk_info_temp, blk_info, sizeof(*blk_info_temp));
	if (blk_info->part_occupancy == 1) {
		//Free the block
		blk_info_temp->valid = 1;
		blk_info_temp->occupied = 0;
		block_temp->header.crc = INVALID_CRC;
		block_temp->header.part_size = INVALID_PART_SIZE;
	} else {
		const struct block *blk = BLOCK(block_num);
		blk_info_temp->part_occupancy--;
		memcpy(block_temp, blk, BLK_SIZE);
		memcpy(get_part(block_temp, blk_info_temp, index), get_part(block_temp, blk_info_temp, blk_info_temp->part_occupancy), blk_info_temp->part_size * SUB_BLK_SIZE);
		memcpy(block_temp->uid_tbl + index, block_temp->uid_tbl + blk_info_temp->part_occupancy, sizeof(struct uid_ent));
	}
	return block_num;
}

static int update_uid(int uid, u8 *data, int sz, int *prev_block_num, const u8 *iv, struct block *block_temp, struct block_info *blk_info_temp)
{
	if (sz == 0) {
		return deallocate_uid(uid, block_temp, blk_info_temp);
	} else {
		int block_num;
		int index;
		const struct uid_ent *ent;
		ent = find_uid(uid, &block_num, &index);
		if (!ent) {
			*prev_block_num = INVALID_BLOCK;
			block_num = allocate_uid(uid, data, sz, 0, iv, block_temp, blk_info_temp);
			return block_num;
		} else {
			int part_size = target_part_size(sz);
			struct block_info *blk_info = block_info_tbl + block_num;
			memcpy(blk_info_temp, blk_info, sizeof(*blk_info_temp));
			if (blk_info_temp->part_size == part_size) {
				//shuffle parts
				const struct block *blk = BLOCK(block_num);
				memcpy(block_temp, blk, BLK_SIZE);
				*prev_block_num = block_num;
				deallocate_uid(uid, block_temp, blk_info_temp);
				allocate_uid_blk(uid, data, sz, ent->rev, iv, block_temp, blk_info_temp);
				return block_num;
			} else {
				//move parts
				int new_rev = (ent->rev + 1) & 0x3;
				*prev_block_num = block_num;
				block_num = allocate_uid(uid, data, sz, new_rev, iv, block_temp, blk_info_temp);
				return block_num;
			}
		}
	}
}

void update_uid_cmd(int uid, u8 *data, int sz) 
{
	derive_iv(uid, cmd_data.update_uid.iv);
	struct block *block = (struct block *)&cmd_data.set_data.block;
	cmd_data.update_uid.sz = sz;
	cmd_data.update_uid.write_count = 0;
	cmd_data.update_uid.block_num = update_uid(uid,
			data,
			sz,
			&cmd_data.update_uid.prev_block_num,
			cmd_data.update_uid.iv,	
			block,
		        &cmd_data.update_uid.blk_info);
	if (cmd_data.update_uid.block_num == INVALID_BLOCK) {
		finish_command_resp(NOT_ENOUGH_SPACE);
	} else {
		begin_button_press_wait();
	}
}

void update_uid_cmd_complete()
{
	struct block *block = (struct block *)cmd_data.update_uid.block;
	if (cmd_data.update_uid.blk_info.occupied) {
		block->header.crc = block_crc(block);
	}
	flash_write_page((u8 *)BLOCK(cmd_data.update_uid.block_num), (u8 *)block, BLK_SIZE);
}

void update_uid_cmd_write_finished()
{
	cmd_data.update_uid.write_count++;
	if (cmd_data.update_uid.write_count == 1) {
		memcpy(block_info_tbl + cmd_data.update_uid.block_num, &cmd_data.update_uid.blk_info, sizeof(struct block_info));
		if (cmd_data.update_uid.prev_block_num == cmd_data.update_uid.block_num && cmd_data.update_uid.prev_block_num != INVALID_BLOCK) {
			struct block *block = (struct block *)cmd_data.update_uid.block;
			deallocate_uid(cmd_data.update_uid.uid, block, &cmd_data.update_uid.blk_info); 
			if (cmd_data.update_uid.blk_info.occupied) {
				block->header.crc = block_crc(block);
			}
			flash_write_page((u8 *)BLOCK(cmd_data.update_uid.prev_block_num), (u8 *)block, BLK_SIZE);
		} else {
			if (!cmd_data.update_uid.sz) {
				uid_map[cmd_data.update_uid.uid] = INVALID_BLOCK;
			}
			finish_command_resp(OKAY);
		}
	} else {
		memcpy(block_info_tbl + cmd_data.update_uid.prev_block_num, &cmd_data.update_uid.blk_info, sizeof(struct block_info));
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

static void read_uid(u8 *dest, int block_num, int index)
{
	int blk_count = SIZE_TO_SUB_BLK_COUNT(cmd_data.read_uid.ent->sz);
	const struct block *blk = BLOCK(cmd_data.read_uid.block_num);
	struct block_info *blk_info = block_info_tbl + index;
	stm_aes_256_decrypt_cbc(encrypt_key, blk_count, cmd_data.read_uid.iv, cmd_data.read_uid.block, get_part((struct block *)blk, blk_info, cmd_data.read_uid.index));
	if (cmd_data.read_uid.masked) {
		mask_uid_data(cmd_data.read_uid.block, blk_count);
	}
}

void read_uid_cmd(int uid, int masked)
{
	cmd_data.read_uid.uid = uid;
	cmd_data.read_uid.masked = masked;
	derive_iv(uid, cmd_data.read_uid.iv);

	cmd_data.read_uid.ent = find_uid(uid, &cmd_data.read_uid.block_num, &cmd_data.read_uid.index);

	if (!cmd_data.read_uid.ent) {
		finish_command_resp(ID_INVALID);
		return;
	}

	if (!masked) {
		begin_button_press_wait();
	} else {
		read_uid_cmd_complete();
	}
}

void read_uid_cmd_complete()
{
	read_uid(cmd_data.read_uid.block, cmd_data.read_uid.block_num, cmd_data.read_uid.index);
	finish_command(OKAY, cmd_data.get_data.block, cmd_data.get_data.sz);
}

void read_all_uids_cmd_iter()
{
	u8 *block = cmd_data.read_all_uids.block;
	int block_num;
	const struct uid_ent *ent;
	int index;
	do {
		while (uid_map[cmd_data.read_all_uids.uid] == 0 && cmd_data.read_all_uids.uid < MAX_UID)
			cmd_data.read_all_uids.uid++;

		if (cmd_data.read_all_uids.uid == MAX_UID) {
			block[0] = MAX_UID & 0xff;
			block[1] = MAX_UID >> 8;
			finish_command_multi(OKAY, 0, block, 2);
			return;
		}
		ent = find_uid(cmd_data.read_all_uids.uid, &block_num, &index);
		cmd_data.read_all_uids.expected_remaining--;
	} while(!ent);

	block[0] = cmd_data.read_all_uids.uid & 0xff;
	block[1] = cmd_data.read_all_uids.uid >> 8;
	read_uid(cmd_data.read_all_uids.block, block_num, index);

	finish_command_multi(OKAY, cmd_data.read_all_uids.expected_remaining, block, ent->sz + 2);
}

void read_all_uids_cmd_complete()
{
	read_all_uids_cmd_iter();
}

void read_all_uids_cmd(int masked)
{
	cmd_data.read_all_uids.uid = 0;
	cmd_data.read_all_uids.masked = masked;
	cmd_data.read_all_uids.expected_remaining = 0;
	
	for (int i = 0; i < MAX_UID; i++)
		if(uid_map[i])
			cmd_data.read_all_uids.expected_remaining++;

	if (!masked) {
		begin_long_button_press_wait();
	} else {
		read_all_uids_cmd_iter();
	}
}
