#include "usbd_msc_bot.h"
#include "usbd_msc_scsi.h"
#include "usbd_msc.h"
#include "usbd_msc_data.h"
#include "stm32f7xx_hal.h"
#include "commands.h"
#include "buffer_manager.h"
#include "usbd_multi.h"
#include "memory_layout.h"
#include "main.h"
extern struct bufferFIFO usbBulkBufferFIFO;

static int8_t SCSI_TestUnitReady(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Inquiry(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReadFormatCapacity(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_RequestSense (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_StartStopUnit(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ModeSense6 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ModeSense10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Write10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Read10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Verify10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_CheckAddressRange (USBD_HandleTypeDef *pdev, uint8_t lun,
                                      uint32_t blk_offset, uint32_t blk_nbr);

static int8_t SCSI_ProcessRead (USBD_HandleTypeDef *pdev, uint8_t lun);
static int8_t SCSI_ProcessWrite (USBD_HandleTypeDef *pdev, uint8_t lun);

int g_num_scsi_volumes;
int g_scsi_num_regions;
int g_scsi_region_size_blocks;
struct scsi_volume g_scsi_volume[MAX_SCSI_VOLUMES];

USBD_HandleTypeDef  *s_pdev;

extern MMC_HandleTypeDef hmmc1;
static volatile int mmcStageIdx;
static volatile int mmcReadLen;
static u8 *mmcBufferRead;
static const u8 *mmcBufferWrite;
static volatile int mmcBlockAddr = -1;
static volatile int mmcDataToTransfer;
static volatile int mmcSubDataToTransfer;
static volatile int mmcBlocksToTransfer;
static volatile int mmcDataTransferred;
static volatile int mmcTransferActive = 0;

#ifdef BOOT_MODE_B

static int g_cryptStageIdx;
static int g_cryptTxLen;
int g_cryptDataToTransfer;
extern CRYP_HandleTypeDef hcryp;
static u8 g_scsi_cur_aes_iv[AES_BLK_SIZE];
static u32 g_scsi_cur_aes_sector;
static u32 g_scsi_num_aes_sector;
static u32 g_scsi_aes_encrypt;
static u32 *g_scsi_aes_read;
static u32 *g_scsi_aes_write;
static CRYP_ConfigTypeDef g_scsi_aes_crypt_conf;
static void set_crypt_config();

#endif

void usbd_scsi_device_state_change(enum device_state state)
{
	for (int i = 0; i < g_num_scsi_volumes; i++) {
		struct scsi_volume *v = g_scsi_volume + i;
		if (!v->flags & HC_VOLUME_FLAG_VALID) {
			v->visible = 0;
			v->writable = 0;
			continue;
		}
		if (v->flags & HC_VOLUME_FLAG_READ_ONLY) {
			if (v->flags & HC_VOLUME_FLAG_VISIBLE_ON_UNLOCK) {
				switch (state) {
				case DS_LOGGED_OUT:
				case DS_ERASING_PAGES:
				case DS_WIPING:
				case DS_INITIALIZING:
				case DS_UNINITIALIZED:
				case DS_DISCONNECTED:
				case DS_RESTORING_DEVICE:
				case DS_BOOTLOADER:
					v->writable = 0;
					break;
				case DS_LOGGED_IN:
				case DS_FIRMWARE_UPDATE:
				case DS_BACKING_UP_DEVICE:
				default:
					v->writable = 1;
					break;
				}
			}
		}
		if (v->flags & HC_VOLUME_FLAG_HIDDEN) {
			if (v->flags & HC_VOLUME_FLAG_VISIBLE_ON_UNLOCK) {
				switch (state) {
				case DS_LOGGED_OUT:
				case DS_ERASING_PAGES:
				case DS_WIPING:
				case DS_INITIALIZING:
				case DS_UNINITIALIZED:
				case DS_DISCONNECTED:
				case DS_RESTORING_DEVICE:
				case DS_BOOTLOADER:
					v->visible = 0;
					break;
				case DS_LOGGED_IN:
				case DS_FIRMWARE_UPDATE:
				case DS_BACKING_UP_DEVICE:
				default:
					v->visible = 1;
					break;
				}
			}
		}
	}
}

void usbd_scsi_init()
{
	u32 nr_blocks  = hmmc1.MmcCard.BlockNbr - (EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ));
	g_scsi_region_size_blocks = (STORAGE_REGION_SIZE)/hmmc1.MmcCard.BlockSize;
	g_num_scsi_volumes = 2;
	g_scsi_num_regions = nr_blocks / g_scsi_region_size_blocks;

	g_scsi_volume[0].nr = 2;
	g_scsi_volume[0].flags = HC_VOLUME_FLAG_VALID;
	g_scsi_volume[0].region_start = 0;
	g_scsi_volume[0].n_regions = (8192/32);
	g_scsi_volume[0].started = 0;
	g_scsi_volume[0].visible = 1;
	g_scsi_volume[0].writable = 1;

	g_scsi_volume[1].nr = 2;
	g_scsi_volume[1].flags = HC_VOLUME_FLAG_VALID |
		HC_VOLUME_FLAG_ENCRYPTED |
		HC_VOLUME_FLAG_HIDDEN |
		HC_VOLUME_FLAG_VISIBLE_ON_UNLOCK;
	g_scsi_volume[1].region_start = g_scsi_volume[0].n_regions;
	g_scsi_volume[1].n_regions = g_scsi_num_regions - g_scsi_volume[0].n_regions;
	g_scsi_volume[1].started = 0;
	g_scsi_volume[1].visible = 0;
	g_scsi_volume[1].writable = 1;
}

int8_t SCSI_ProcessCmd(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *cmd)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	switch (cmd[0]) {
	case SCSI_TEST_UNIT_READY:
		return SCSI_TestUnitReady(pdev, lun, cmd);
		break;

	case SCSI_REQUEST_SENSE:
		return SCSI_RequestSense (pdev, lun, cmd);
		break;
	case SCSI_INQUIRY:
		return SCSI_Inquiry(pdev, lun, cmd);
		break;

	case SCSI_START_STOP_UNIT:
		return SCSI_StartStopUnit(pdev, lun, cmd);
		break;

	case SCSI_ALLOW_MEDIUM_REMOVAL:
		return SCSI_StartStopUnit(pdev, lun, cmd);
		break;

	case SCSI_MODE_SENSE6:
		return SCSI_ModeSense6 (pdev, lun, cmd);
		break;

	case SCSI_MODE_SENSE10:
		return SCSI_ModeSense10 (pdev, lun, cmd);
		break;

	case SCSI_READ_FORMAT_CAPACITIES:
		return SCSI_ReadFormatCapacity(pdev, lun, cmd);
		break;

	case SCSI_READ_CAPACITY10:
		return SCSI_ReadCapacity10(pdev, lun, cmd);
		break;

	case SCSI_READ10:
		return SCSI_Read10(pdev, lun, cmd);
		break;

	case SCSI_WRITE10:
		return SCSI_Write10(pdev, lun, cmd);
		break;

	case SCSI_VERIFY10:
		return SCSI_Verify10(pdev, lun, cmd);
		break;

	default:
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_CDB, 0);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1;
	}
	return 0;
}

static int8_t SCSI_TestUnitReady(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	/* case 9 : Hi > D0 */
	if (hmsc->cbw.dDataLength != 0U) {
		SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
		return -1;
	}

	if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1;
	}
	hmsc->bot_data_length = 0U;
	return 0;
}

static int8_t  SCSI_Inquiry(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	uint8_t* pPage;
	uint16_t len;
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if (params[1] & 0x01U) { /*Evpd is set*/
		len = LENGTH_INQUIRY_PAGE00;
		hmsc->bot_data_length = len;

		//TODO: We don't like using this bot_data array
		while (len) {
			len--;
			hmsc->bot_data[len] = MSC_Page00_Inquiry_Data[len];
		}
	} else {
		pPage = (uint8_t *)(void *)&((USBD_StorageTypeDef *)pdev->pUserData)->pInquiry[0 * STANDARD_INQUIRY_DATA_LEN];
		len = (uint16_t)pPage[4] + 5U;

		if (params[4] <= len) {
			len = params[4];
		}
		hmsc->bot_data_length = len;

		//TODO: We don't like using this bot_data array
		while (len) {
			len--;
			hmsc->bot_data[len] = pPage[len];
		}
	}

	return 0;
}

static u8 capacity_resp[8] __attribute__((aligned(16)));

static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if(((USBD_StorageTypeDef *)pdev->pUserData)->GetCapacity(lun, &hmsc->scsi_blk_nbr, &hmsc->scsi_blk_size) != 0) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
		USBD_LL_StallEP(pdev, MSC_EPIN_ADDR);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1;
	} else {
		capacity_resp[0] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >> 24);
		capacity_resp[1] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >> 16);
		capacity_resp[2] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >>  8);
		capacity_resp[3] = (uint8_t)(hmsc->scsi_blk_nbr - 1U);
		capacity_resp[4] = (uint8_t)(hmsc->scsi_blk_size >>  24);
		capacity_resp[5] = (uint8_t)(hmsc->scsi_blk_size >>  16);
		capacity_resp[6] = (uint8_t)(hmsc->scsi_blk_size >>  8);
		capacity_resp[7] = (uint8_t)(hmsc->scsi_blk_size);
		uint16_t length = (uint16_t)MIN(hmsc->cbw.dDataLength, 8);
		hmsc->csw.dDataResidue -= length;
		hmsc->csw.bStatus = USBD_CSW_CMD_PASSED;
		hmsc->bot_state = USBD_BOT_SEND_DATA;
		hmsc->bot_data_length = 0;
		USBD_LL_Transmit(pdev, MSC_EPIN_ADDR, capacity_resp, length);
		return 0;
	}
}

static int8_t SCSI_ReadFormatCapacity(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	uint16_t blk_size;
	uint32_t blk_nbr;
	uint16_t i;

	uint8_t *bot_data = hmsc->bot_data;

	//TODO: We don't like using this bot_data array
	for(i = 0U; i < 12U ; i++) {
		bot_data[i] = 0U;
	}

	if(((USBD_StorageTypeDef *)pdev->pUserData)->GetCapacity(lun, &blk_nbr, &blk_size) != 0U) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
		USBD_LL_StallEP(pdev, MSC_EPIN_ADDR);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1;
	} else {
		bot_data[8] = 0x02U;
	}
	bot_data[3] = 0x08U;
	bot_data[4] = (uint8_t)((blk_nbr - 1U) >> 24);
	bot_data[5] = (uint8_t)((blk_nbr - 1U) >> 16);
	bot_data[6] = (uint8_t)((blk_nbr - 1U) >>  8);
	bot_data[7] = (uint8_t)(blk_nbr - 1U);
	bot_data[9] = (uint8_t)(blk_size >>  16);
	bot_data[10] = (uint8_t)(blk_size >>  8);
	bot_data[11] = (uint8_t)(blk_size);

	hmsc->bot_data_length = 12U;
	return 0;
}

static int8_t SCSI_ModeSense6 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
#if 0
	uint16_t len = 8U;
	hmsc->bot_data_length = len;

	while (len) {
		len--;
		hmsc->bot_data[len] = MSC_Mode_Sense6_data[len];
	}
#endif
	uint16_t length = (uint16_t)MIN(hmsc->cbw.dDataLength, 8U);
	hmsc->csw.dDataResidue -= length;
	hmsc->csw.bStatus = USBD_CSW_CMD_PASSED;
	hmsc->bot_state = USBD_BOT_SEND_DATA;
	hmsc->bot_data_length = 0;
	USBD_LL_Transmit(pdev, MSC_EPIN_ADDR, MSC_Mode_Sense6_data, length);
	return 0;
}

static int8_t SCSI_ModeSense10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	uint16_t len = 8U;
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
#if 0
	hmsc->bot_data_length = len;

	while (len) {
		len--;
		hmsc->bot_data[len] = MSC_Mode_Sense10_data[len];
	}
#endif
	uint16_t length = (uint16_t)MIN(hmsc->cbw.dDataLength, 8U);
	hmsc->csw.dDataResidue -= length;
	hmsc->csw.bStatus = USBD_CSW_CMD_PASSED;
	hmsc->bot_state = USBD_BOT_SEND_DATA;
	hmsc->bot_data_length = 0;
	USBD_LL_Transmit(pdev, MSC_EPIN_ADDR, MSC_Mode_Sense10_data, length);
	return 0;
}

static u8 sense_data[REQUEST_SENSE_DATA_LEN];

static int8_t SCSI_RequestSense (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	uint8_t i;
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	if(hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		for(i = 0U ; i < REQUEST_SENSE_DATA_LEN; i++) {
			sense_data[i] = 0U;
		}

		sense_data[0]	= 0x70U;
		sense_data[7]	= 10;//REQUEST_SENSE_DATA_LEN - 7U;

		if((hmsc->scsi_sense_head != hmsc->scsi_sense_tail)) {

			sense_data[2]     = hmsc->scsi_sense[hmsc->scsi_sense_head].Skey;
			sense_data[12]    = hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASC;
			sense_data[13]    = hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASCQ;
			hmsc->scsi_sense_head++;

			if (hmsc->scsi_sense_head == SENSE_LIST_DEEPTH) {
				hmsc->scsi_sense_head = 0U;
			}
		}
		int len = REQUEST_SENSE_DATA_LEN;
		if (params[4] <= REQUEST_SENSE_DATA_LEN) {
			len = params[4];
		}
		uint16_t length = (uint16_t)MIN(hmsc->cbw.dDataLength, len);
		hmsc->csw.dDataResidue -= length;
		hmsc->csw.bStatus = USBD_CSW_CMD_PASSED;
		hmsc->bot_state = USBD_BOT_SEND_DATA;
		hmsc->bot_data_length = 0;
		USBD_LL_Transmit(pdev, MSC_EPIN_ADDR, sense_data, length);
	}
	return 0;
}

void SCSI_SenseCode(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t sKey, uint8_t ASC, uint8_t ASCQ)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];

	hmsc->scsi_sense[hmsc->scsi_sense_tail].Skey  = sKey;
	hmsc->scsi_sense[hmsc->scsi_sense_tail].w.b.ASC = ASC;
	hmsc->scsi_sense[hmsc->scsi_sense_tail].w.b.ASCQ = ASCQ;
	hmsc->scsi_sense_tail++;
	if (hmsc->scsi_sense_tail == SENSE_LIST_DEEPTH) {
		hmsc->scsi_sense_tail = 0U;
	}
}

struct start_stop_unit {
	u8 cmd;
	u8 immed;
	u8 reserved;
	u8 pwr_modifier;
	u8 params;
	u8 control;
} __attribute__((packed));

static int8_t SCSI_StartStopUnit(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	//
	// This command has a number of parameters but we can ignore all except the power condition
	// and the start bit. If the power state is being changed then we are supposed to ignore the
	// start bit, otherwise we should respond to it.
	//

	//IMMED: We will always return status immediately so this flag has no meaning for us
	//LOEJ: Ejecting the medium is not a meaningful concept for a virtual volume
	//NO_FLUSH: We aren't performing any cacheing so this is irrelevant
	//
	struct start_stop_unit *ssu = (struct start_stop_unit *)(params);
	if ((ssu->params & 0xf0) == 0 && (ssu->pwr_modifier & 1) == 0) {
		if (ssu->params & 0x1) {
			//Start unit
		} else {
			//Stop unit
		}
	}
	hmsc->bot_data_length = 0U;
	return 0;
}

void readProcessingComplete(struct bufferFIFO *bf)
{
	MSC_BOT_SendCSW (s_pdev, USBD_CSW_CMD_PASSED);
	assert_lit(mmcDataToTransfer == 0, 1, 1);
}

int g_usb_transmitting = 0;

static int8_t SCSI_ProcessRead (USBD_HandleTypeDef  *pdev, uint8_t lun)
{
	if (g_usb_transmitting) {
		g_usb_transmitting = 0;
		USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];
		uint32_t len = MIN(hmsc->scsi_blk_len, hmsc->writeLen);
		hmsc->scsi_blk_addr += len;
		hmsc->scsi_blk_len -= len;
		hmsc->csw.dDataResidue -= len;
		if (hmsc->scsi_blk_len == 0) {
			bufferFIFO_stallStage(&usbBulkBufferFIFO, hmsc->stageIdx);
		}
		bufferFIFO_processingComplete(&usbBulkBufferFIFO, hmsc->stageIdx, len, 0);
		return 0;
	}
	return 0;
}

static void processUSBReadBuffer(struct bufferFIFO *bf, int readSize, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	g_usb_transmitting = 1;
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) s_pdev->pClassData[INTERFACE_MSC];
	hmsc->stageIdx = stageIdx;
	hmsc->writeBuffer = bufferWrite;
	hmsc->writeLen = readSize;
	USBD_LL_Transmit(s_pdev, MSC_EPIN_ADDR, hmsc->writeBuffer, hmsc->writeLen);
}

static void processMMCReadBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	uint32_t len;
	if (mmcDataToTransfer <= bf->maxBufferSize) {
		len = mmcDataToTransfer;
	} else {
		len = bf->maxBufferSize;
	}
	mmcBufferRead = bufferWrite;
	mmcStageIdx = stageIdx;
	mmcReadLen = len;
	mmcTransferActive = 1;
	emmc_user_queue(EMMC_USER_STORAGE);
	emmc_user_schedule();
}

#ifdef BOOT_MODE_B

static void set_crypt_config()
{
	derive_iv(g_scsi_cur_aes_sector, g_scsi_cur_aes_iv);
	g_scsi_aes_crypt_conf.DataType = CRYP_DATATYPE_32B;
	g_scsi_aes_crypt_conf.KeySize = CRYP_KEYSIZE_128B;
	g_scsi_aes_crypt_conf.pKey = (u32 *)g_encrypt_key;
	g_scsi_aes_crypt_conf.pInitVect = (u32 *)g_scsi_cur_aes_iv;
	g_scsi_aes_crypt_conf.Algorithm = CRYP_AES_CBC;
	g_scsi_aes_crypt_conf.DataWidthUnit = CRYP_DATAWIDTHUNIT_WORD;
	HAL_CRYP_SetConfig(&hcryp, &g_scsi_aes_crypt_conf);
}

static void processDecryptReadBuffer(struct bufferFIFO *bf,
		int readLen, u32 readData,
		const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	g_cryptStageIdx = stageIdx;
	g_cryptTxLen = readLen;
	assert((readLen % 512) == 0);
	g_scsi_num_aes_sector = readLen / 512;
	g_scsi_aes_read = (u32 *)bufferRead;
	g_scsi_aes_write = (u32 *)bufferWrite;
	g_scsi_aes_encrypt = 0;
	set_crypt_config();
	HAL_CRYP_Decrypt_DMA(&hcryp, g_scsi_aes_read, 512/4, g_scsi_aes_write);
}

#endif

void emmc_user_read_storage_rx_complete()
{
	mmcTransferActive = 0;
	mmcDataToTransfer -= mmcReadLen;
	mmcDataTransferred += mmcReadLen;
	mmcBlockAddr += mmcReadLen/512;
	mmcBlocksToTransfer -= mmcReadLen/512;

	if (mmcDataToTransfer == 0) {
		bufferFIFO_stallStage(&usbBulkBufferFIFO, mmcStageIdx);
	}
	emmc_user_done();
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, mmcStageIdx, mmcReadLen, 0);
}

static int8_t SCSI_Read10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	s_pdev = pdev;
	if(hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 10 : Ho <> Di */
		if ((hmsc->cbw.bmFlags & 0x80U) != 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		uint64_t blk_addr = ((uint64_t)params[2] << 24) |
		                    ((uint64_t)params[3] << 16) |
		                    ((uint64_t)params[4] <<  8) |
		                    (uint64_t)params[5];

		uint64_t blk_len = ((uint64_t)params[7] <<  8) | (uint64_t)params[8];

		if(SCSI_CheckAddressRange(pdev, lun, blk_addr,
		                          blk_len) < 0) {
			return -1; /* error */
		}
		hmsc->scsi_blk_addr = blk_addr * hmsc->scsi_blk_size;
		hmsc->scsi_blk_len = blk_len * hmsc->scsi_blk_size;
		hmsc->bot_state = USBD_BOT_DATA_IN;

		/* cases 4,5 : Hi <> Dn */
		if (hmsc->cbw.dDataLength != hmsc->scsi_blk_len) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}
		uint32_t len = MIN(hmsc->scsi_blk_len, usbBulkBufferFIFO.maxBufferSize);
		mmcDataToTransfer = hmsc->scsi_blk_len;
		mmcBlocksToTransfer = blk_len;
		mmcBlockAddr = blk_addr;
		mmcDataTransferred = 0;
#ifdef BOOT_MODE_B
		if (g_scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED) {
			g_cryptDataToTransfer = hmsc->scsi_blk_len;
			g_scsi_cur_aes_sector = blk_addr;
			usbBulkBufferFIFO.numStages = 3;
			usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
			usbBulkBufferFIFO.processStage[1] = processDecryptReadBuffer;
			usbBulkBufferFIFO.processStage[2] = processUSBReadBuffer;
			usbBulkBufferFIFO.processingComplete = readProcessingComplete;
		} else {
			usbBulkBufferFIFO.numStages = 2;
			usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
			usbBulkBufferFIFO.processStage[1] = processUSBReadBuffer;
			usbBulkBufferFIFO.processingComplete = readProcessingComplete;
		}
#else
		usbBulkBufferFIFO.numStages = 2;
		usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
		usbBulkBufferFIFO.processStage[1] = processUSBReadBuffer;
		usbBulkBufferFIFO.processingComplete = readProcessingComplete;
#endif
		bufferFIFO_start(&usbBulkBufferFIFO, len);
		return 0;
	}
	return SCSI_ProcessRead(pdev, lun);
}

void emmc_user_storage_start()
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) s_pdev->pClassData[INTERFACE_MSC];
	HAL_MMC_CardStateTypeDef cardState;
	if (hmsc->bot_state == USBD_BOT_DATA_IN) {
		int lun = hmsc->cbw.bLUN;

		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		u32 blockAddrAdj = mmcBlockAddr +
			(EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ)) +
			(g_scsi_volume[lun].region_start * g_scsi_region_size_blocks);

		HAL_MMC_ReadBlocks_DMA(&hmmc1, mmcBufferRead,
				blockAddrAdj,
				mmcReadLen/512);
	} else if (hmsc->bot_state == USBD_BOT_DATA_OUT) {
		int lun = hmsc->cbw.bLUN;

		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		u32 blockAddrAdj = mmcBlockAddr +
			(EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ)) +
			(g_scsi_volume[lun].region_start * g_scsi_region_size_blocks);

		HAL_MMC_WriteBlocks_DMA_Initial(&hmmc1, mmcBufferWrite, mmcReadLen,
				blockAddrAdj,
				mmcReadLen/512);
	}
}

void writeProcessingComplete(struct bufferFIFO *bf)
{
	MSC_BOT_SendCSW (s_pdev, USBD_CSW_CMD_PASSED);
	assert_lit(mmcDataToTransfer == 0, 1, 1);
}

void processUSBWriteBuffer(struct bufferFIFO *bf, int readSize, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) s_pdev->pClassData[INTERFACE_MSC];
	uint32_t len;
	if (hmsc->scsi_blk_len <= bf->maxBufferSize) {
		len = hmsc->scsi_blk_len;
	} else {
		len = bf->maxBufferSize;
	}
	hmsc->stageIdx = stageIdx;
	hmsc->writeBuffer = bufferWrite;
	hmsc->writeLen = len;
	USBD_LL_PrepareReceive (s_pdev, MSC_EPOUT_ADDR, bufferWrite, len);
}

#ifdef BOOT_MODE_B

static int g_cryptOutInt = 0;

void HAL_CRYP_OutCpltCallback(CRYP_HandleTypeDef *hcryp)
{
	g_cryptOutInt = 1;
}

#endif

int usbd_scsi_idle_ready()
{
#ifdef BOOT_MODE_B
	return g_cryptOutInt;
#else
	return 0;
#endif
}

void usbd_scsi_idle()
{
#ifdef BOOT_MODE_B
	if (g_cryptOutInt) {
		g_cryptOutInt = 0;
		g_scsi_cur_aes_sector++;
		g_scsi_num_aes_sector--;
		g_scsi_aes_read += 512/4;
		g_scsi_aes_write += 512/4;
		if (!g_scsi_num_aes_sector) {
			g_cryptDataToTransfer -= g_cryptTxLen;
			if (g_cryptDataToTransfer == 0) {
				bufferFIFO_stallStage(&usbBulkBufferFIFO, g_cryptStageIdx);
			}
			bufferFIFO_processingComplete(&usbBulkBufferFIFO, g_cryptStageIdx, g_cryptTxLen, 0);
		} else {
			set_crypt_config();
			if (g_scsi_aes_encrypt) {
				HAL_CRYP_Encrypt_DMA(&hcryp, g_scsi_aes_read, 512/4, g_scsi_aes_write);
			} else {
				HAL_CRYP_Decrypt_DMA(&hcryp, g_scsi_aes_read, 512/4, g_scsi_aes_write);
			}
		}
	}
#endif
}

#ifdef BOOT_MODE_B
void processEncryptWriteBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	g_cryptStageIdx = stageIdx;
	g_cryptTxLen = readLen;

	assert((readLen % 512) == 0);
	g_scsi_num_aes_sector = readLen / 512;
	g_scsi_aes_read = (u32 *)bufferRead;
	g_scsi_aes_write = (u32 *)bufferWrite;
	g_scsi_aes_encrypt = 1;
	set_crypt_config();
	HAL_CRYP_Encrypt_DMA(&hcryp, g_scsi_aes_read, 512/4, g_scsi_aes_write);
}
#endif

int mmcTXDmaActive = 0;

void emmc_user_write_storage_tx_dma_complete(MMC_HandleTypeDef *hmmc)
{
	mmcDataToTransfer -= mmcReadLen;
	mmcDataTransferred += mmcReadLen;
	mmcBlockAddr += mmcReadLen/512;
	mmcBlocksToTransfer -= mmcReadLen/512;
	//TODO: handle return error code
	HAL_MMC_WriteBlocks_DMA_Cont(&hmmc1, NULL, 0);
}

void HAL_MMC_AbortCallback(MMC_HandleTypeDef *hmmc)
{
	UNUSED(hmmc);
}

void processMMCWriteBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	mmcStageIdx = stageIdx;
	mmcReadLen = readLen;
	while (mmcTransferActive);
	mmcTransferActive = 1;
	mmcBufferWrite = bufferRead;
	emmc_user_queue(EMMC_USER_STORAGE);
	emmc_user_schedule();
}

int mmcShortWriteCount = 0;

void emmc_user_write_storage_tx_complete(MMC_HandleTypeDef *hmmc1)
{
	mmcTransferActive = 0;
	if (mmcDataToTransfer == 0) {
		bufferFIFO_stallStage(&usbBulkBufferFIFO, mmcStageIdx);
	}
	emmc_user_done();
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, mmcStageIdx, mmcReadLen, 0);
}

static int8_t SCSI_Write10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	s_pdev = pdev;

	if (hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 8 : Hi <> Do */
		if ((hmsc->cbw.bmFlags & 0x80U) == 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		/* Check whether Media is ready */
		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		/* Check If media is write-protected */
		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsWriteProtected(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, WRITE_PROTECTED, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		hmsc->scsi_blk_addr = ((uint32_t)params[2] << 24) |
		                      ((uint32_t)params[3] << 16) |
		                      ((uint32_t)params[4] << 8) |
		                      (uint32_t)params[5];

		hmsc->scsi_blk_len = ((uint32_t)params[7] << 8) |
		                     (uint32_t)params[8];
		uint32_t blk_addr = hmsc->scsi_blk_addr;
		uint32_t blk_len = hmsc->scsi_blk_len;

		/* check if LBA address is in the right range */
		if(SCSI_CheckAddressRange(pdev, lun, hmsc->scsi_blk_addr,
		                          hmsc->scsi_blk_len) < 0) {
			return -1; /* error */
		}

		hmsc->scsi_blk_addr *= hmsc->scsi_blk_size;
		hmsc->scsi_blk_len  *= hmsc->scsi_blk_size;

		/* cases 3,11,13 : Hn,Ho <> D0 */
		if (hmsc->cbw.dDataLength != hmsc->scsi_blk_len) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}
		hmsc->bot_state = USBD_BOT_DATA_OUT;

		/* Prepare EP to receive first data packet */
		uint32_t len = MIN(hmsc->scsi_blk_len, usbBulkBufferFIFO.maxBufferSize);
		mmcDataToTransfer = hmsc->scsi_blk_len;
		mmcBlocksToTransfer = blk_len;
		mmcBlockAddr = blk_addr;
		mmcDataTransferred = 0;
#ifdef BOOT_MODE_B
		if (g_scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED) {
			g_cryptDataToTransfer = hmsc->scsi_blk_len;
			g_scsi_cur_aes_sector = blk_addr;
			usbBulkBufferFIFO.numStages = 3;
			usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
			usbBulkBufferFIFO.processStage[1] = processEncryptWriteBuffer;
			usbBulkBufferFIFO.processStage[2] = processMMCWriteBuffer;
			usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
		} else {
			usbBulkBufferFIFO.numStages = 2;
			usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
			usbBulkBufferFIFO.processStage[1] = processMMCWriteBuffer;
			usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
		}
#else
		usbBulkBufferFIFO.numStages = 2;
		usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
		usbBulkBufferFIFO.processStage[1] = processMMCWriteBuffer;
		usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
#endif
		bufferFIFO_start(&usbBulkBufferFIFO, len);
	} else { /* Write Process ongoing */
		return SCSI_ProcessWrite(pdev, lun);
	}
	return 0;
}


/**
* @brief  SCSI_Verify10
*         Process Verify10 command
* @param  lun: Logical unit number
* @param  params: Command parameters
* @retval status
*/

static int8_t SCSI_Verify10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if ((params[1]& 0x02U) == 0x02U) {
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELED_IN_COMMAND, 0);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1; /* Error, Verify Mode Not supported*/
	}

	if(SCSI_CheckAddressRange(pdev, lun, hmsc->scsi_blk_addr,
	                          hmsc->scsi_blk_len) < 0) {
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1; /* error */
	}
	hmsc->bot_data_length = 0U;
	return 0;
}

/**
* @brief  SCSI_CheckAddressRange
*         Check address range
* @param  lun: Logical unit number
* @param  blk_offset: first block address
* @param  blk_nbr: number of block to be processed
* @retval status
*/
static int8_t SCSI_CheckAddressRange (USBD_HandleTypeDef *pdev, uint8_t lun,
                                      uint32_t blk_offset, uint32_t blk_nbr)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if ((blk_offset + blk_nbr) > hmsc->scsi_blk_nbr) {
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, ADDRESS_OUT_OF_RANGE, 0);
		hmsc->bot_data_length = 0U;
		hmsc->bot_state = USBD_BOT_NO_DATA;
		return -1;
	}
	return 0;
}

extern PCD_HandleTypeDef hpcd;

static int8_t SCSI_ProcessWrite (USBD_HandleTypeDef  *pdev, uint8_t lun)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	uint32_t len = MIN(hmsc->scsi_blk_len, hmsc->writeLen);
	hmsc->scsi_blk_addr += len;
	hmsc->scsi_blk_len -= len;
	hmsc->csw.dDataResidue -= len;
	if (hmsc->scsi_blk_len == 0) {
		bufferFIFO_stallStage(&usbBulkBufferFIFO, hmsc->stageIdx);
	}
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, hmsc->stageIdx, len, 0);
	return 0;
}
