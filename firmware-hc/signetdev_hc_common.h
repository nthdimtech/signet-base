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

struct hc_firmware_file {
	u32 file_version;
	u8 firmware_hash_key[HC_FIRMWARE_HASH_KEY_LEN];
	u8 firmware_hash[HC_FIRMWARE_HASH_LEN];
	struct hc_firmware_version fw_version;
	u32 firmware_A_crc;
	u32 firmware_B_crc;
	u32 firmware_A_len;
	u32 firmware_B_len;
	u8 firmware_signature_pubkey[HC_FIRMWARE_SIGNATURE_PUBKEY_LEN];
	u8 firmware_A_signature[HC_FIRMWARE_SIGNATURE_LEN];
	u8 firmware_B_signature[HC_FIRMWARE_SIGNATURE_LEN];
	u8 firmware_A[HC_BOOT_AREA_A_LEN];
	u8 firmware_B[HC_BOOT_AREA_B_LEN];
} __attribute__((packed));

#endif
