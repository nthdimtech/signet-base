#ifndef MEMORY_LAYOUT_H
#define MEMORY_LAYOUT_H

#include "types.h"

#define FLASH_BASE_ADDR (0x8000000)
#define BOOT_AREA_A (0x8008000)
#define BOOT_AREA_A_LEN (1024 * 96)
#define BOOT_AREA_B (0x8020000)
#define BOOT_AREA_B_LEN (1024 * 384)
#define DATA_AREA_1 (0x8008000)
#define DATA_AREA_2 (0x8020000)

#define DEVICE_ID_LEN (16)
#define DEVICE_NAME_LEN (32)
#define FIRMWARE_HASH_KEY_LEN (32)
#define FIRMWARE_HASH_LEN (32)
#define FIRMWARE_SIGNATURE_LEN (32)
#define FIRMWARE_SIGNATURE_PUBKEY_LEN (32)
#define AUTH_RANDOM_DATA_LEN (16)
#define MAX_PROFILES (16)
#define MAX_TAG_NAME_LENGTH (16)
#define HASH_FUNCTION_PARAMS_LENGTH (16)
#define KEYSTORE_KEY_SIZE (32)
#define MAX_PROFILE_NAME_LENGTH (32)
#define MAX_PROFILE_TAGS (16)
#define HC_HASH_FN_SALT_SZ (32)

#define EMMC_SUB_BLOCK_SZ (512)
#define HC_BLOCK_SZ (16384)
#define EMMC_DB_FIRST_BLOCK (4)
#define EMMC_DB_NUM_BLOCK (1024)
#define EMMC_STORAGE_FIRST_BLOCK (EMMC_DB_NUM_BLOCK + EMMC_DB_FIRST_BLOCK)

struct hcdb_tag {
	u16 id;
	u8 tag_name[MAX_TAG_NAME_LENGTH];
	u16 user_data_size;
	u8 user_data[];
} __attribute__((packed));

struct hcdb_profile {
	u16 version;
	u16 reserved;
	u16 id;
	u16 parent_id;
	u8 profile_name[MAX_PROFILE_NAME_LENGTH];
	u16 num_tags;
	u16 tag_ids[MAX_PROFILE_TAGS];
	u16 user_data_size;
	u8 user_data[];
} __attribute__((packed));

struct hcdb_profile_definition_block_footer {
	u16 num_profiles;
	struct hcdb_profile profiles[];
} __attribute__((packed));

struct hcdb_profile_definition_block {
	u16 num_tags;
	struct hcdb_tag tags[];
	// The structure hcdb_profile_definition_block_footer
	// follows the tags. It stores the list of profiles
} __attribute__((packed));

struct hcdb_profile_auth_data {
	u8 salt[HC_HASH_FN_SALT_SZ];
	u8 hash_function_params[HASH_FUNCTION_PARAMS_LENGTH];
	u8 auth_random_cyphertext[AUTH_RANDOM_DATA_LEN];
	u8 keystore_key_cyphertext[KEYSTORE_KEY_SIZE];
} __attribute__((packed));

struct hc_firmware_version {
	u16 major;
	u16 minor;
	u16 step;
} __attribute__((packed));

enum hc_firmware_upgrade_state {
	HC_FIRMWARE_VALID,
	HC_FILE_DOWNLOADED,
	HC_BOOTLOADER_ONLY_UPGRADED,
	HC_APPLICATION_ONLY_UPGRADED
};

struct hc_device_data {
	u32 data_crc;
	u16 format;
	u16 db_format;
	u16 data_iteration; //larger is newer
	u8 device_id[DEVICE_ID_LEN];
	u8 device_name[DEVICE_NAME_LEN];
	struct hc_firmware_version fw_version[2];
	u8 firmware_hash_key[2][FIRMWARE_HASH_KEY_LEN];
	u8 firmware_hash[2][FIRMWARE_HASH_LEN];
	u32 firmware_A_crc[2];
	u32 firmware_B_crc[2];
	u32 firmware_A_signature[2][FIRMWARE_SIGNATURE_LEN];
	u32 firmware_B_signature[2][FIRMWARE_SIGNATURE_LEN];
	u32 firmware_signature_pubkey[FIRMWARE_SIGNATURE_PUBKEY_LEN];
	u8 auth_random_cleartext[AUTH_RANDOM_DATA_LEN];
	struct hcdb_profile_auth_data profile_auth_data[MAX_PROFILES];
	u16 user_data_size;
	u8 user_data[];
} __attribute__((packed));

#endif
