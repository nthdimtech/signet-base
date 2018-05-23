#ifndef SIGNETDEV_COMMON_H
#define SIGNETDEV_COMMON_H

enum commands {
	STARTUP,
	OPEN_ID, //Defunct
	CLOSE_ID, //Defunct
	GET_DATA, //Defunct
	SET_DATA, //Defunct
	TYPE,
	LOGIN,
	LOGOUT,
	DELETE_ID,
	BUTTON_WAIT,
	GET_PROGRESS,
	GET_DEVICE_STATE,
	ERASE_FLASH_PAGES,
	WRITE_FLASH,
	RESET_DEVICE,
	INITIALIZE,
	CONNECT, //Defunct
	DISCONNECT,
	WIPE,
	CANCEL_BUTTON_PRESS,
	CHANGE_MASTER_PASSWORD,
	BACKUP_DEVICE,
	READ_BLOCK,
	BACKUP_DEVICE_DONE,
	RESTORE_DEVICE,
	WRITE_BLOCK,
	ERASE_BLOCK,
	RESTORE_DEVICE_DONE,
	GET_DEVICE_CAPACITY,
	UPDATE_FIRMWARE,
	GET_ALL_DATA,
	UPDATE_UID,
	READ_UID,
	READ_ALL_UIDS,
	GET_RAND_BITS,
	ENTER_MOBILE_MODE,
	LOGIN_TOKEN,
	READ_CLEARTEXT_PASSWORDS,
	WRITE_CLEARTEXT_PASSWORDS
};

enum device_state {
	DISCONNECTED,
	RESET, //Defunct
	UNINITIALIZED,
	INITIALIZING,
	WIPING,
	ERASING_PAGES,
	FIRMWARE_UPDATE,
	LOGGED_OUT,
	LOGGED_IN,
	BACKING_UP_DEVICE,
	RESTORING_DEVICE
};

enum command_responses {
	OKAY,
	INVALID_STATE,
	INVALID_INPUT,
	ID_INVALID,
	ID_NOT_OPEN,
	TAG_INVALID,
	WRITE_FAILED,
	NOT_LOGGED_IN,
	NOT_INITIALIZED,
	BAD_PASSWORD,
	IN_PROGRESS,
	NOT_ENOUGH_SPACE,
	DONE,
	BUTTON_PRESS_CANCELED,
	BUTTON_PRESS_TIMEOUT,
	UNKNOWN_DB_FORMAT,
	DEVICE_NOT_WIPED
};

#define SIGNET_MAJOR_VERSION 1
#define SIGNET_MINOR_VERSION 3
#define SIGNET_STEP_VERSION 1

#define SIGNET_ERROR_UNKNOWN -1
#define SIGNET_ERROR_DISCONNECT -2
#define SIGNET_ERROR_QUIT -3
#define SIGNET_ERROR_OVERFLOW -4

#define AES_BLK_SIZE (16)
#define AES_128_KEY_SIZE (16)
#define AES_256_KEY_SIZE (32)

#define SUB_BLK_SIZE (AES_BLK_SIZE)
#define SUB_BLK_MASK_SIZE (2)
#define SUB_BLK_DATA_SIZE (14)

#define SIZE_TO_SUB_BLK_COUNT(sz) (((sz) + (SUB_BLK_DATA_SIZE - 1))/SUB_BLK_DATA_SIZE)
#define SUB_BLK_COUNT(sz) (((sz) + (SUB_BLK_SIZE - 1))/SUB_BLK_SIZE)

#define BLK_SIZE (2048)
#define SUB_BLK_PER_BLOCK ((BLK_SIZE/SUB_BLK_SIZE) - 1)
#define BLK_USER_SIZE (SUB_BLK_PER_BLOCK * SUB_BLK_DATA_SIZE)
#define BLK_MASK_SIZE (SUB_BLK_PER_BLOCK * SUB_BLK_MASK_SIZE)

#define LOGIN_KEY_SZ (AES_256_KEY_SIZE)
#define SALT_SZ_V1 (AES_128_KEY_SIZE)
#define SALT_SZ_V2 (AES_256_KEY_SIZE)
#define HASH_FN_SZ (AES_BLK_SIZE)
#define CBC_IV_SZ (AES_BLK_SIZE)
#define INIT_RAND_DATA_SZ ((AES_256_KEY_SIZE * 2) + CBC_IV_SZ)
#define INITIALIZE_CMD_SIZE (LOGIN_KEY_SZ + HASH_FN_SZ + SALT_SZ_V2 + INIT_RAND_DATA_SZ)

#define STARTUP_RESP_SIZE (6 + HASH_FN_SZ + SALT_SZ_V2)

#define TOTAL_STORAGE_SIZE (192 * 1024)
#define NUM_STORAGE_BLOCKS (TOTAL_STORAGE_SIZE/BLK_SIZE)

#define MIN_DATA_BLOCK 1
#define MAX_DATA_BLOCK (NUM_STORAGE_BLOCKS-1)
#define NUM_DATA_BLOCKS (MAX_DATA_BLOCK - MIN_DATA_BLOCK + 1)

#define INVALID_UID (0)
#define MAX_UID ((1<<12)-1)
#define MIN_UID (1)

#define USB_VENDOR_ID (0x5E2A)
#define USB_SIGNET_DESKTOP_PRODUCT_ID (0x0001)
#define USB_SIGNET_MOBILE_PRODUCT_ID (0x0001)
#define USB_REV_ID (0x0483)
#define USB_RAW_HID_USAGE_PAGE	0xFFAB	// recommended: 0xFF00 to 0xFFFF
#define USB_RAW_HID_USAGE	0x0200	// recommended: 0x0100 to 0xFFFF
#define RAW_HID_PACKET_SIZE 64
#define RAW_HID_HEADER_SIZE 1
#define RAW_HID_PAYLOAD_SIZE (RAW_HID_PACKET_SIZE - RAW_HID_HEADER_SIZE)

#define CMD_PACKET_HEADER_SIZE 6
#define CMD_PACKET_PAYLOAD_SIZE (BLK_SIZE + CMD_PACKET_HEADER_SIZE + 1)
#define CMD_PACKET_BUF_SIZE (CMD_PACKET_PAYLOAD_SIZE + CMD_PACKET_HEADER_SIZE + RAW_HID_PAYLOAD_SIZE - 1)

#include <stdint.h>
typedef unsigned long long u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

struct cleartext_pass {
	u8 format;
	u8 length;
	u8 data[126];
};
#define NUM_CLEARTEXT_PASS 4
#define CLEARTEXT_PASS_SIZE 128

struct root_page
{
	u8 signature[AES_BLK_SIZE];
	union {
		struct {
			u32 crc;
			u8 db_version;
			u8 auth_rand[AES_256_KEY_SIZE];
			u8 auth_rand_ct[AES_256_KEY_SIZE];
			u8 encrypt_key_ct[AES_256_KEY_SIZE];
			u8 cbc_iv[AES_BLK_SIZE];
			u8 salt[SALT_SZ_V2];
			u8 hashfn[HASH_FN_SZ];
			struct cleartext_pass cleartext_passwords[NUM_CLEARTEXT_PASS];
		} v2;
	} header;
} __attribute__((__packed__));

struct db_uid_ent {
	u16 info; //[0:11] = uid, [12:13] = rev, [14] = first, [15] = padding/reserved
	u16 sz;
	u16 blk_next;
} __attribute__((__packed__));

struct db_block_header {
	u32 crc;
	u16 part_size;
	u16 occupancy;
} __attribute__((__packed__));

struct db_block {
	struct db_block_header header;
	struct db_uid_ent uid_tbl[];
} __attribute__((__packed__));

#define INVALID_BLOCK (0)
#define INVALID_PART_SIZE (0xffff)
#define INVALID_CRC (0xffffffff)

#endif
