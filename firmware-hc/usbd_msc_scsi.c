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

int8_t SCSI_ProcessCmd(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *cmd)
{
	switch (cmd[0]) {
	case SCSI_TEST_UNIT_READY:
		SCSI_TestUnitReady(pdev, lun, cmd);
		break;

	case SCSI_REQUEST_SENSE:
		SCSI_RequestSense (pdev, lun, cmd);
		break;
	case SCSI_INQUIRY:
		SCSI_Inquiry(pdev, lun, cmd);
		break;

	case SCSI_START_STOP_UNIT:
		SCSI_StartStopUnit(pdev, lun, cmd);
		break;

	case SCSI_ALLOW_MEDIUM_REMOVAL:
		SCSI_StartStopUnit(pdev, lun, cmd);
		break;

	case SCSI_MODE_SENSE6:
		SCSI_ModeSense6 (pdev, lun, cmd);
		break;

	case SCSI_MODE_SENSE10:
		SCSI_ModeSense10 (pdev, lun, cmd);
		break;

	case SCSI_READ_FORMAT_CAPACITIES:
		SCSI_ReadFormatCapacity(pdev, lun, cmd);
		break;

	case SCSI_READ_CAPACITY10:
		SCSI_ReadCapacity10(pdev, lun, cmd);
		break;

	case SCSI_READ10:
		SCSI_Read10(pdev, lun, cmd);
		break;

	case SCSI_WRITE10:
		SCSI_Write10(pdev, lun, cmd);
		break;

	case SCSI_VERIFY10:
		SCSI_Verify10(pdev, lun, cmd);
		break;

	default:
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_CDB);
		return -1;
	}
	return 0;
}

static int8_t SCSI_TestUnitReady(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	/* case 9 : Hi > D0 */
	if (hmsc->cbw.dDataLength != 0U) {
		SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);

		return -1;
	}

	if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
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

		while (len) {
			len--;
			hmsc->bot_data[len] = MSC_Page00_Inquiry_Data[len];
		}
	} else {
		pPage = (uint8_t *)(void *)&((USBD_StorageTypeDef *)pdev->pUserData)->pInquiry[lun * STANDARD_INQUIRY_DATA_LEN];
		len = (uint16_t)pPage[4] + 5U;

		if (params[4] <= len) {
			len = params[4];
		}
		hmsc->bot_data_length = len;

		while (len) {
			len--;
			hmsc->bot_data[len] = pPage[len];
		}
	}

	return 0;
}

static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if(((USBD_StorageTypeDef *)pdev->pUserData)->GetCapacity(lun, &hmsc->scsi_blk_nbr, &hmsc->scsi_blk_size) != 0) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
		return -1;
	} else {
		uint8_t *bot_data = hmsc->bot_data;
		bot_data[0] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >> 24);
		bot_data[1] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >> 16);
		bot_data[2] = (uint8_t)((hmsc->scsi_blk_nbr - 1U) >>  8);
		bot_data[3] = (uint8_t)(hmsc->scsi_blk_nbr - 1U);

		bot_data[4] = (uint8_t)(hmsc->scsi_blk_size >>  24);
		bot_data[5] = (uint8_t)(hmsc->scsi_blk_size >>  16);
		bot_data[6] = (uint8_t)(hmsc->scsi_blk_size >>  8);
		bot_data[7] = (uint8_t)(hmsc->scsi_blk_size);

		hmsc->bot_data_length = 8U;
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

	for(i = 0U; i < 12U ; i++) {
		bot_data[i] = 0U;
	}

	if(((USBD_StorageTypeDef *)pdev->pUserData)->GetCapacity(lun, &blk_nbr, &blk_size) != 0U) {
		SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
		return -1;
	} else {
		bot_data[3] = 0x08U;
		bot_data[4] = (uint8_t)((blk_nbr - 1U) >> 24);
		bot_data[5] = (uint8_t)((blk_nbr - 1U) >> 16);
		bot_data[6] = (uint8_t)((blk_nbr - 1U) >>  8);
		bot_data[7] = (uint8_t)(blk_nbr - 1U);

		bot_data[8] = 0x02U;
		bot_data[9] = (uint8_t)(blk_size >>  16);
		bot_data[10] = (uint8_t)(blk_size >>  8);
		bot_data[11] = (uint8_t)(blk_size);

		hmsc->bot_data_length = 12U;
		return 0;
	}
}

static int8_t SCSI_ModeSense6 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	uint16_t len = 8U;
	hmsc->bot_data_length = len;

	while (len) {
		len--;
		hmsc->bot_data[len] = MSC_Mode_Sense6_data[len];
	}
	return 0;
}

static int8_t SCSI_ModeSense10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	uint16_t len = 8U;
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	hmsc->bot_data_length = len;

	while (len) {
		len--;
		hmsc->bot_data[len] = MSC_Mode_Sense10_data[len];
	}

	return 0;
}

static int8_t SCSI_RequestSense (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	uint8_t i;
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	uint8_t *bot_data = hmsc->bot_data;

	for(i = 0U ; i < REQUEST_SENSE_DATA_LEN; i++) {
		bot_data[i] = 0U;
	}

	bot_data[0]	= 0x70U;
	bot_data[7]	= REQUEST_SENSE_DATA_LEN - 6U;

	if((hmsc->scsi_sense_head != hmsc->scsi_sense_tail)) {

		bot_data[2]     = hmsc->scsi_sense[hmsc->scsi_sense_head].Skey;
		bot_data[12]    = hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASCQ;
		bot_data[13]    = hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASC;
		hmsc->scsi_sense_head++;

		if (hmsc->scsi_sense_head == SENSE_LIST_DEEPTH) {
			hmsc->scsi_sense_head = 0U;
		}
	}
	hmsc->bot_data_length = REQUEST_SENSE_DATA_LEN;

	if (params[4] <= REQUEST_SENSE_DATA_LEN) {
		hmsc->bot_data_length = params[4];
	}
	return 0;
}

void SCSI_SenseCode(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t sKey, uint8_t ASC)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];

	hmsc->scsi_sense[hmsc->scsi_sense_tail].Skey  = sKey;
	hmsc->scsi_sense[hmsc->scsi_sense_tail].w.ASC = ASC << 8;
	hmsc->scsi_sense_tail++;
	if (hmsc->scsi_sense_tail == SENSE_LIST_DEEPTH) {
		hmsc->scsi_sense_tail = 0U;
	}
}

static int8_t SCSI_StartStopUnit(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	hmsc->bot_data_length = 0U;
	return 0;
}

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
		bufferFIFO_processingComplete(&usbBulkBufferFIFO, hmsc->stageIdx, len);
		return 0;
	}
	return 0;
}

static void processUSBReadBuffer(struct bufferFIFO *bf, int readSize, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	g_usb_transmitting = 1;
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) s_pdev->pClassData[INTERFACE_MSC];
	hmsc->stageIdx = stageIdx;
	hmsc->writeBuffer = bufferWrite;
	hmsc->writeLen = readSize;
	USBD_LL_Transmit(s_pdev, MSC_EPIN_ADDR, hmsc->writeBuffer, hmsc->writeLen);
}

static void processMMCReadBuffer(struct bufferFIFO *bf, int readLen, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
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
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, mmcStageIdx, mmcReadLen);
}

static int8_t SCSI_Read10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	s_pdev = pdev;
	if(hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 10 : Ho <> Di */
		if ((hmsc->cbw.bmFlags & 0x80U) != 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
			return -1;
		}

		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
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
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
			return -1;
		}
		uint32_t len = MIN(hmsc->scsi_blk_len, usbBulkBufferFIFO.maxBufferSize);
		mmcDataToTransfer = hmsc->scsi_blk_len;
		mmcBlocksToTransfer = blk_len;
		mmcBlockAddr = blk_addr;
		mmcDataTransferred = 0;
		usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
		usbBulkBufferFIFO.processStage[1] = processUSBReadBuffer;
		usbBulkBufferFIFO.processingComplete = readProcessingComplete;
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
		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);
		HAL_MMC_ReadBlocks_DMA(&hmmc1, mmcBufferRead,
				mmcBlockAddr + EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ),
				mmcReadLen/512);
	} else if (hmsc->bot_state == USBD_BOT_DATA_OUT) {
		do {
			cardState = HAL_MMC_GetCardState(&hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		HAL_MMC_WriteBlocks_DMA_Initial(&hmmc1, mmcBufferWrite, mmcReadLen,
				mmcBlockAddr + EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ),
				mmcReadLen/512);
	}
}

void writeProcessingComplete(struct bufferFIFO *bf)
{
	MSC_BOT_SendCSW (s_pdev, USBD_CSW_CMD_PASSED);
	assert_lit(mmcDataToTransfer == 0, 1, 1);
}

void processUSBWriteBuffer(struct bufferFIFO *bf, int readSize, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
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

void processMMCWriteBuffer(struct bufferFIFO *bf, int readLen, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
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
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, mmcStageIdx, mmcReadLen);
}

static int8_t SCSI_Write10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	s_pdev = pdev;

	if (hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 8 : Hi <> Do */
		if ((hmsc->cbw.bmFlags & 0x80U) == 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
			return -1;
		}

		/* Check whether Media is ready */
		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsReady(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
			return -1;
		}

		/* Check If media is write-protected */
		if(((USBD_StorageTypeDef *)pdev->pUserData)->IsWriteProtected(lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, WRITE_PROTECTED);
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
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
			return -1;
		}
		hmsc->bot_state = USBD_BOT_DATA_OUT;

		/* Prepare EP to receive first data packet */
		uint32_t len = MIN(hmsc->scsi_blk_len, usbBulkBufferFIFO.maxBufferSize);
		mmcDataToTransfer = hmsc->scsi_blk_len;
		mmcBlocksToTransfer = blk_len;
		mmcBlockAddr = blk_addr;
		mmcDataTransferred = 0;
		usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
		usbBulkBufferFIFO.processStage[1] = processMMCWriteBuffer;
		usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
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
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELED_IN_COMMAND);
		return -1; /* Error, Verify Mode Not supported*/
	}

	if(SCSI_CheckAddressRange(pdev, lun, hmsc->scsi_blk_addr,
	                          hmsc->scsi_blk_len) < 0) {
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
		SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, ADDRESS_OUT_OF_RANGE);
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
	bufferFIFO_processingComplete(&usbBulkBufferFIFO, hmsc->stageIdx, len);
	return 0;
}
