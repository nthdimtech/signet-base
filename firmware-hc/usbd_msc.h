#ifndef __USBD_MSC_H
#define __USBD_MSC_H

#ifdef __cplusplus
extern "C" {
#endif

#include  "usbd_msc_bot.h"
#include  "usbd_msc_scsi.h"
#include  "usbd_ioreq.h"
#include  "usbd_multi.h"
#include  "buffer_manager.h"
#include  "memory_layout.h"

/* MSC Class Config */
#ifndef MSC_MEDIA_PACKET
#define MSC_MEDIA_PACKET             512U
#endif /* MSC_MEDIA_PACKET */

#define MSC_MAX_FS_PACKET            0x40U
#define MSC_MAX_HS_PACKET            0x200U

#define BOT_GET_MAX_LUN              0xFE
#define BOT_RESET                    0xFF
#define USB_MSC_CONFIG_DESC_SIZ      32

struct scsi_volume {
	int nr;
	u32 flags;
	u32 region_start;
	u32 n_regions;
	u8 volume_name[MAX_VOLUME_NAME_LEN];
	int started;
	int visible;
	int writable;
};

typedef struct USBD_MSC_BOT {
	uint32_t                 max_lun;
	uint8_t                  bot_data[MSC_MEDIA_PACKET * 16] __attribute__((aligned(4)));
	uint32_t                 interface;
	uint16_t                 bot_data_length;
	uint8_t                  bot_state;
	uint8_t                  bot_status;


	int stageIdx;
	uint8_t *writeBuffer;
	int writeLen;
	int usb_transmitting;
	struct bufferFIFO usbBulkBufferFIFO;
	int num_scsi_volumes;
	int scsi_num_regions;
	int scsi_region_size_blocks;
	struct scsi_volume scsi_volume[MAX_SCSI_VOLUMES];

	USBD_MSC_BOT_CBWTypeDef  cbw __attribute__((aligned(4)));
	USBD_MSC_BOT_CSWTypeDef  csw __attribute__((aligned(4)));

	USBD_SCSI_SenseTypeDef   scsi_sense [SENSE_LIST_DEEPTH];
	uint8_t                  scsi_sense_head;
	uint8_t                  scsi_sense_tail;

	uint16_t                 scsi_blk_size;
	uint32_t                 scsi_blk_nbr;

	uint64_t                 scsi_blk_addr;
	uint64_t                 scsi_blk_len;
}
USBD_MSC_BOT_HandleTypeDef;

/* Structure for MSC process */
extern USBD_ClassTypeDef  USBD_MSC;
#define USBD_MSC_CLASS    &USBD_MSC

uint8_t  USBD_MSC_Setup (USBD_HandleTypeDef *pdev,
                         USBD_SetupReqTypedef *req);

uint8_t  USBD_MSC_DataIn (USBD_HandleTypeDef *pdev,
                          uint8_t epnum);


uint8_t  USBD_MSC_DataOut (USBD_HandleTypeDef *pdev,
                           uint8_t epnum);

#ifdef __cplusplus
}
#endif

#endif  /* __USBD_MSC_H */
/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
