#ifndef __USBD_MSC_H
#define __USBD_MSC_H

#ifdef __cplusplus
extern "C" {
#endif

#include  "usbd_msc_bot.h"
#include  "usbd_msc_scsi.h"
#include  "usbd_ioreq.h"
#include  "usbd_multi.h"

uint8_t  *USBD_MSC_GetHSCfgDesc (uint16_t *length);
uint8_t  *USBD_MSC_GetFSCfgDesc (uint16_t *length);
uint8_t  *USBD_MSC_GetOtherSpeedCfgDesc (uint16_t *length);
uint8_t  USBD_MSC_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);
uint8_t  USBD_MSC_DataOut (USBD_HandleTypeDef *pdev,
                           uint8_t epnum);
uint8_t  USBD_MSC_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
uint8_t  USBD_MSC_DeInit (USBD_HandleTypeDef *pdev,
                          uint8_t cfgidx);
uint8_t  USBD_MSC_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

/** @addtogroup USBD_MSC_BOT
  * @{
  */

/** @defgroup USBD_MSC
  * @brief This file is the Header file for usbd_msc.c
  * @{
  */


/** @defgroup USBD_BOT_Exported_Defines
  * @{
  */
/* MSC Class Config */
#ifndef MSC_MEDIA_PACKET
#define MSC_MEDIA_PACKET             512U
#endif /* MSC_MEDIA_PACKET */

#define MSC_MAX_FS_PACKET            0x40U
#define MSC_MAX_HS_PACKET            0x200U

#define BOT_GET_MAX_LUN              0xFE
#define BOT_RESET                    0xFF
#define USB_MSC_CONFIG_DESC_SIZ      32

/**
  * @}
  */

/** @defgroup USB_CORE_Exported_Types
  * @{
  */
typedef struct _USBD_STORAGE {
	int8_t (* Init) (uint8_t lun);
	int8_t (* GetCapacity) (uint8_t lun, uint32_t *block_num, uint16_t *block_size);
	int8_t (* IsReady) (uint8_t lun);
	int8_t (* IsWriteProtected) (uint8_t lun);
	int8_t (* Read) (uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
	int8_t (* Write)(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
	int8_t (* GetMaxLun)(void);
	int8_t *pInquiry;

} USBD_StorageTypeDef;

typedef struct {
	uint32_t                 max_lun;
	uint8_t                  bot_data[MSC_MEDIA_PACKET * 16] __attribute__((aligned(4)));
	uint32_t                 interface;
	uint16_t                 bot_data_length;
	uint8_t                  bot_state;
	uint8_t                  bot_status;


	int stageIdx;
	uint8_t *writeBuffer;
	int writeLen;

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

uint8_t  USBD_MSC_RegisterStorage  (USBD_HandleTypeDef   *pdev,
                                    USBD_StorageTypeDef *fops);
/**
  * @}
  */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif  /* __USBD_MSC_H */
/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
