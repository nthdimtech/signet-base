#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
typedef unsigned char u8;
#include "signetdev_hc_common.h"
#include <zlib.h>

struct hc_firmware_file_header fw_file_hdr;
struct hc_firmware_file_body fw_file_body;

int main(int argc, char **argv)
{
	if (argc <= 5)
		return -1;
	FILE *fwA = fopen(argv[1],"rb");
	FILE *fwB = fopen(argv[2],"rb");
	FILE *fwOut = fopen(argv[3],"wb");
	fseek(fwA, 0L, SEEK_END);
	int szA = ftell(fwA);
	fseek(fwA, 0, SEEK_SET);
	
	fseek(fwB, 0L, SEEK_END);
	int szB = ftell(fwB);
	fseek(fwB, 0, SEEK_SET);

	memset(&fw_file_hdr, 0, sizeof(fw_file_hdr));
	memset(&fw_file_body, 0, sizeof(fw_file_body));

	fread(fw_file_body.firmware_A, 1, szA, fwA);
	fread(fw_file_body.firmware_B, 1, szB, fwB);
	
	fw_file_hdr.fw_version.major = atoi(argv[4]);
	fw_file_hdr.fw_version.minor = atoi(argv[5]);
	fw_file_hdr.fw_version.step = atoi(argv[6]);
	fw_file_hdr.fw_version.padding = 0;;
	fw_file_hdr.file_prefix = HC_FIRMWARE_FILE_PREFIX;
	fw_file_hdr.file_version = HC_FIRMWARE_FILE_VERSION;
	fw_file_hdr.header_size = sizeof(fw_file_hdr);
	fw_file_hdr.A_len = szA;
	fw_file_hdr.A_crc = crc32(0, fw_file_body.firmware_A, sizeof(fw_file_body.firmware_A));
	fw_file_hdr.B_len = szB;
	fw_file_hdr.B_crc = crc32(0, fw_file_body.firmware_B, sizeof(fw_file_body.firmware_B));

	fwrite(&fw_file_hdr, 1, sizeof(fw_file_hdr),fwOut);
	fwrite(&fw_file_body, 1, sizeof(fw_file_body),fwOut);
	fclose(fwOut);
}
