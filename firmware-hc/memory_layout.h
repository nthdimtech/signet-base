#ifndef MEMORY_LAYOUT_H
#define MEMORY_LAYOUT_H

#include "types.h"
#include "signetdev_hc_common.h"

#define FLASH_BASE_ADDR (0x8000000)
#define BOOT_AREA_A (0x8008000)
#define BOOT_AREA_B (0x8020000)
#define DATA_AREA_1 (0x8000000)
#define DATA_AREA_2 (0x8004000)

#define DEVICE_ID_LEN (16)
#define DEVICE_NAME_LEN (32)
#define AUTH_RANDOM_DATA_LEN (16)
#define MAX_PROFILES (16)
#define MAX_TAG_NAME_LENGTH (16)
#define MAX_PROFILE_NAME_LENGTH (32)
#define MAX_PROFILE_TAGS (16)

#define EMMC_SUB_BLOCK_SZ (512)
#define HC_BLOCK_SZ (1<<14)
#define HC_FIRMWARE_HEADER_SZ (HC_BLOCK_SZ)
#define HC_FIRMWARE_SZ (1<<19)
#define EMMC_DB_FIRMWARE_UPDATE_BLOCK (0)
#define EMMC_DB_FIRMWARE_UPDATE_BLOCKS ((HC_FIRMWARE_HEADER_SZ + HC_FIRMWARE_SZ)/HC_BLOCK_SZ)
#define EMMC_DB_KEYSTORE_BLOCK (EMMC_DB_FIRMWARE_UPDATE_BLOCK + EMMC_DB_FIRMWARE_UPDATE_BLOCKS)
#define EMMC_DB_KEYSTORE_BLOCKS (4)
#define EMMC_DB_FIRST_BLOCK (EMMC_DB_KEYSTORE_BLOCK + EMMC_DB_KEYSTORE_BLOCKS)
#define EMMC_DB_NUM_BLOCK (1024 + 4)
#define EMMC_STORAGE_FIRST_BLOCK (EMMC_DB_NUM_BLOCK + EMMC_DB_FIRST_BLOCK)

#define NUM_STORAGE_REGIONS (1024)
#define MAX_TAGS (16)
#define MAX_VOLUME_NAME_LEN (32)
#define MAX_VOLUMES (10) //Includes "free space volume" and "primary volume" which can't be deleted

enum hc_volumes {
	VOL_FREE_SPACE,
	VOL_PRIMARY_VIRTUAL,
	VOL_CLIENT_STORAGE,
};

struct hcdb_tag {
	u32 id;
	u8 tag_name[MAX_TAG_NAME_LENGTH];
} __attribute__((packed));

struct hcdb_profile {
	u16 version;
	u16 reserved;
	u16 id;
	u16 parent_id;
	u8 profile_name[MAX_PROFILE_NAME_LENGTH];
	u32 num_tags;
	u16 tag_ids[MAX_PROFILE_TAGS];
} __attribute__((packed));

struct hcdb_profile_definition_block {
	u32 num_tags;
	struct hcdb_tag tags[MAX_TAGS];
	u32 num_profiles;
	struct hcdb_profile profiles[MAX_PROFILES];
} __attribute__((packed));

struct hcdb_profile_auth_data {
	u8 salt[HC_HASH_FN_SALT_SZ];
	u8 hash_function_params[HC_HASH_FUNCTION_PARAMS_LENGTH];
	u8 auth_random_cyphertext[AUTH_RANDOM_DATA_LEN];
	u8 keystore_key_cyphertext[HC_KEYSTORE_KEY_SIZE];
} __attribute__((packed));

enum hc_firmware_upgrade_state {
	HC_FIRMWARE_VALID,
	HC_FILE_DOWNLOADED,
	HC_BOOTLOADER_ONLY_UPGRADED,
	HC_APPLICATION_ONLY_UPGRADED
};

#define HC_VOLUME_FLAG_READ_ONLY (1<<0)
#define HC_VOLUME_FLAG_WRITABLE_ON_UNLOCK (1<<1)
#define HC_VOLUME_FLAG_WRITABLE_ON_REQUEST (1<<2)
#define HC_VOLUME_FLAG_HIDDEN (1<<3)
#define HC_VOLUME_FLAG_VISIBLE_ON_UNLOCK (1<<4)
#define HC_VOLUME_FLAG_VISIBLE_ON_REQUEST (1<<5)
#define HC_VOLUME_FLAG_ONE_TIME_USE (1<<6)
#define HC_VOLUME_FLAG_ENCRYPTED (1<<7)
#define HC_VOLUME_FLAG_USE_KEYSTORE (1<<8)
#define HC_VOLUME_FLAG_VIRTUAL (1<<9)

struct hc_volume {
	u32 flags;
	u32 n_regions;
	u8 volume_name[MAX_VOLUME_NAME_LEN];
};

struct hc_device_data {
	u32 crc; //This must be the first entry
	u16 format;
	u16 db_format;
	u16 data_iteration; //larger is newer
	u8 device_id[DEVICE_ID_LEN];
	u8 device_name[DEVICE_NAME_LEN];
	u32 upgrade_state;
	struct hc_firmware_version fw_version[2];
	u8 firmware_hash_key[2][HC_FIRMWARE_HASH_KEY_LEN];
	u8 firmware_hash[2][HC_FIRMWARE_HASH_LEN];
	u32 firmware_A_crc[2];
	u32 firmware_B_crc[2];
	u32 firmware_A_signature[2][HC_FIRMWARE_SIGNATURE_LEN];
	u32 firmware_B_signature[2][HC_FIRMWARE_SIGNATURE_LEN];
	u32 firmware_signature_pubkey[HC_FIRMWARE_SIGNATURE_PUBKEY_LEN];
	u8 auth_random_cleartext[AUTH_RANDOM_DATA_LEN];
	struct hcdb_profile_auth_data profile_auth_data[MAX_PROFILES];
	struct hc_volume volumes[MAX_VOLUMES];
	u16 storage_region_map[NUM_STORAGE_REGIONS];

	struct hcdb_profile_definition_block profile_info; //Encrypted
	u32 user_data_len;
	u8 user_data[];
} __attribute__((packed));

#endif
