#ifndef DB_H
#define DB_H

struct block_info {
	u8 valid;
	u8 occupied;
	u8 part_size; //0 == invalid block, 61=>X>=1 == block size = X * 32,, X > 63 == invalid block
	u8 part_count; //# of partitions total in a block
	u8 part_occupancy; //# of partitions total in a block
	u16 part_tbl_offs; // # of occupied partitions in a block
};

void read_uid_cmd(int uid, int masked);
void update_uid_cmd(int uid, u8 *data, int sz);
void read_all_uids_cmd(int masked);

void read_all_uids_cmd_iter();
void read_uid_cmd_complete();
void update_uid_cmd_complete();
void update_uid_cmd_write_finished();

void db2_startup_scan();
#endif
