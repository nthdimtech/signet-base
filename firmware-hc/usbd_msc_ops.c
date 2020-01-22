#include "usbd_msc_ops.h"
#include "usbd_msc_scsi.h"
#include "memory_layout.h"

#define STORAGE_LUN_NBR                  1


int8_t STORAGE_Init (uint8_t lun);

int8_t STORAGE_GetCapacity (uint8_t lun,
                            uint32_t *block_num,
                            uint16_t *block_size);

int8_t  STORAGE_IsReady (uint8_t lun);

int8_t  STORAGE_IsWriteProtected (uint8_t lun);

int8_t STORAGE_Read (uint8_t lun,
                     uint8_t *buf,
                     uint32_t blk_addr,
                     uint16_t blk_len);

int8_t STORAGE_Write (uint8_t lun,
                      uint8_t *buf,
                      uint32_t blk_addr,
                      uint16_t blk_len);

int8_t STORAGE_GetMaxLun (void);

/* USB Mass storage Standard Inquiry Data */
int8_t  STORAGE_Inquirydata[] = {//36

	/* LUN 0 */
	0x00, //Device connected
	0x80, //Removable
	0x02, //Version
	0x02, //SCSI-3
	(STANDARD_INQUIRY_DATA_LEN - 5), //Remaining data
	0x00, //
	0x00, // No special flags
	0x00, //
	'N', 'T', 'H', 'D', 'I', 'M', ' ', ' ', /* Manufacturer : 8 bytes */
	'S', 'i', 'g', 'n', 'e', 't', ' ', 'H', /* Product      : 16 Bytes */
	'C', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
	'0', '.', '0','1',                      /* Version      : 4 Bytes */
};

USBD_StorageTypeDef USBD_MSC_Template_fops = {
	STORAGE_Init,
	STORAGE_GetCapacity,
	STORAGE_IsReady,
	STORAGE_IsWriteProtected,
	STORAGE_Read,
	STORAGE_Write,
	STORAGE_GetMaxLun,
	STORAGE_Inquirydata,

};

int8_t STORAGE_Init (uint8_t lun)
{
	return (0);
}

extern MMC_HandleTypeDef hmmc1;

int8_t STORAGE_GetCapacity (uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
	if (lun < g_num_scsi_volumes && g_scsi_volume[lun].visible) {
		*block_num = (u32)(g_scsi_volume[lun].n_regions * g_scsi_region_size_blocks);
		*block_size = (uint16_t)hmmc1.MmcCard.BlockSize;
		return 0;
	} else {
		return -1;
	}
}

int8_t  STORAGE_IsReady (uint8_t lun)
{
	if (lun < g_num_scsi_volumes) {
#ifdef BOOT_MODE_B
		if (g_scsi_volume[lun].visible)
			return 0;
		else
			return 1;
#else
		if (g_scsi_volume[lun].visible && !(g_scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED)) {
			return 0;
		} else {
			return 1;
		}
#endif
	} else {
		return 1;
	}
}

int8_t  STORAGE_IsWriteProtected (uint8_t lun)
{
	if (lun < g_num_scsi_volumes) {
		if (g_scsi_volume[lun].writable)
			return 0;
		else
			return 1;
	} else {
		return 1;
	}
}

int8_t STORAGE_Read (uint8_t lun,
                     uint8_t *buf,
                     uint32_t blk_addr,
                     uint16_t blk_len)
{
	return 0;
}

int8_t STORAGE_Write (uint8_t lun,
                      uint8_t *buf,
                      uint32_t blk_addr,
                      uint16_t blk_len)
{
	return (0);
}

int8_t STORAGE_GetMaxLun (void)
{
	return (MAX_SCSI_VOLUMES - 1);
}

