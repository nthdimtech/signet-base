#ifndef SIGNETDEV_HC_COMMON_H
#define SIGNETDEV_HC_COMMON_H

#define HC_FIRMWARE_SIGNATURE_PUBKEY_LEN (32)
#define HC_FIRMWARE_SIGNATURE_LEN (32)
#define HC_FIRMWARE_HASH_KEY_LEN (32)
#define HC_FIRMWARE_HASH_LEN (32)
#define HC_HASH_FN_SALT_SZ (32)
#define HC_HASH_FUNCTION_PARAMS_LENGTH (16)
#define HC_KEYSTORE_KEY_SIZE (32)
#define HC_BOOT_AREA_A_LEN (1024 * 96)
#define HC_BOOT_AREA_B_LEN (1024 * 384)

//#include "types.h"

enum hc_boot_mode {
	HC_BOOT_BOOTLOADER_MODE,
	HC_BOOT_APPLICATION_MODE,
        HC_BOOT_UNKNOWN_MODE
};

#define HC_UPGRADING_BOOTLOADER_MASK (1<<0)
#define HC_UPGRADED_BOOTLOADER_MASK (1<<1)
#define HC_UPGRADING_APPLICATION_MASK (1<<2)
#define HC_UPGRADED_APPLICATION_MASK (1<<3)

struct hc_firmware_version {
	u16 major;
	u16 minor;
	u16 step;
	u16 padding;
} __attribute__((packed));

struct hc_firmware_info {
	struct hc_firmware_version fw_version;
	u32 firmware_crc;
	u32 firmware_len;
	u8 firmware_signature[HC_FIRMWARE_SIGNATURE_LEN];
	u8 firmware_signature_pubkey[HC_FIRMWARE_SIGNATURE_PUBKEY_LEN];
} __attribute__((packed));

#define HC_FIRMWARE_FILE_PREFIX (0x99887766)
#define HC_FIRMWARE_FILE_VERSION (1)

struct hc_firmware_file_header {
	u32 file_prefix;
	u32 file_version;
	u32 header_size;
	u8 hash_key[HC_FIRMWARE_HASH_KEY_LEN];
	u8 hash[HC_FIRMWARE_HASH_LEN];
	struct hc_firmware_version fw_version;
	u32 A_crc;
	u32 B_crc;
	u32 A_len;
	u32 B_len;
	u8 signature_pubkey[HC_FIRMWARE_SIGNATURE_PUBKEY_LEN];
	u8 A_signature[HC_FIRMWARE_SIGNATURE_LEN];
	u8 B_signature[HC_FIRMWARE_SIGNATURE_LEN];
} __attribute__((packed));

struct hc_firmware_file_body {
	u8 firmware_A[HC_BOOT_AREA_A_LEN];
	u8 firmware_B[HC_BOOT_AREA_B_LEN];
} __attribute__((packed));

#define SIGNET_HC_MAJOR_VERSION 0
#define SIGNET_HC_MINOR_VERSION 8
#define SIGNET_HC_STEP_VERSION 0

#endif
