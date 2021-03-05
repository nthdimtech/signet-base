#ifndef DB_H
#define DB_H

struct block_info {
	u8 valid;
	u8 occupied;
	u8 part_count; //# of partitions total in a block
	u8 part_occupancy; //# of partitions allocated in a block
	u16 part_size; //0 == invalid block, 61=>X>=1 == block size = X * 16,, X > 63 == invalid block
	u16 part_tbl_offs; // offset of partitions in sub blocks
};

void read_uid_cmd(int uid, int masked);
void update_uid_cmd (int uid, u8 *data, int data_len, int sz, int press_type);
void read_all_uids_cmd(int masked);
void read_all_uids_cmd_iter();

void read_uid_cmd_complete();
void read_all_uids_cmd_complete();
void update_uid_cmd_complete();

void db3_startup_scan(u8 *block_read, struct block_info *blk_info_temp);
struct block *db3_initialize_block(int block_num, struct block *block_temp);

void db3_init();
int db3_read_block_complete(u32 rc);
int db3_write_block_complete(u32 rc);

#define ROOT_BLOCK_FORMAT_CURRENT (3)
#define ROOT_BLOCK_FORMAT_3 (3)
#define DB_FORMAT_CURRENT (3)
#define DB_FORMAT_3 (3)
#define DB_FORMAT_2 (2)

#endif
