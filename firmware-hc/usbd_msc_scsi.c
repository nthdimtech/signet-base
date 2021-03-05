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
extern USBD_HandleTypeDef *g_pdev;
extern MMC_HandleTypeDef g_hmmc1;

//MMC buffer fifo state
volatile static struct {
	int StageIdx;
	int ReadLen;
	u8 *BufferRead;
	const u8 *BufferWrite;
	int BlockAddr;
	int DataToTransfer;
	int SubDataToTransfer;
	int BlocksToTransfer;
	int DataTransferred;
} s_mmc;


#ifdef BOOT_MODE_B

extern CRYP_HandleTypeDef g_hcryp;

//Crypt buffer fifo state
static struct {
	int stageIdx;
	int txLen;
	int dataToTransfer;
	u8 cur_aes_iv[AES_BLK_SIZE];
	u32 cur_aes_sector;
	u32 num_aes_sector;
	u32 aes_encrypt;
	u32 *aes_read;
	u32 *aes_write;
	CRYP_ConfigTypeDef aes_crypt_conf;
} s_scsi_crypt;

static void set_crypt_config();

#endif

/* USB Mass storage Standard Inquiry Data */
static uint8_t SCSI_InquiryData[] = {//36
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

static int8_t SCSI_IsWriteProtected (USBD_MSC_BOT_HandleTypeDef *hmsc, uint8_t lun)
{
	if (lun < hmsc->num_scsi_volumes) {
		if (hmsc->scsi_volume[lun].writable)
			return 0;
		else
			return 1;
	} else {
		return 1;
	}
}

static int8_t SCSI_GetCapacity(USBD_MSC_BOT_HandleTypeDef *hmsc, uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
	if (lun < hmsc->num_scsi_volumes && hmsc->scsi_volume[lun].visible) {
		*block_num = (u32)(hmsc->scsi_volume[lun].n_regions * hmsc->scsi_region_size_blocks);
		*block_size = (uint16_t)g_hmmc1.MmcCard.BlockSize;
		return 0;
	} else {
		return -1;
	}
}

static int8_t SCSI_IsReady (USBD_MSC_BOT_HandleTypeDef *hmsc, uint8_t lun)
{
	if (lun < hmsc->num_scsi_volumes) {
#ifdef BOOT_MODE_B
		if (hmsc->scsi_volume[lun].visible)
			return 0;
		else
			return 1;
#else
		if (hmsc->scsi_volume[lun].visible && !(hmsc->scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED)) {
			return 0;
		} else {
			return 1;
		}
#endif
	} else {
		return 1;
	}
}

void usbd_scsi_device_state_change(enum device_state state)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	for (int i = 0; i < hmsc->num_scsi_volumes; i++) {
		struct scsi_volume *v = hmsc->scsi_volume + i;
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

void usbd_scsi_init(USBD_MSC_BOT_HandleTypeDef  *hmsc)
{
	hmsc->scsi_sense_tail = 0U;
	hmsc->scsi_sense_head = 0U;
	u32 nr_blocks  = g_hmmc1.MmcCard.BlockNbr - (EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ));
	hmsc->scsi_region_size_blocks = (STORAGE_REGION_SIZE)/g_hmmc1.MmcCard.BlockSize;
	hmsc->num_scsi_volumes = 2;
	hmsc->scsi_num_regions = nr_blocks / hmsc->scsi_region_size_blocks;

	hmsc->scsi_volume[0].nr = 2;
	hmsc->scsi_volume[0].flags = HC_VOLUME_FLAG_VALID;
	hmsc->scsi_volume[0].region_start = 0;
	hmsc->scsi_volume[0].n_regions = (8192/32);
	hmsc->scsi_volume[0].started = 0;
	hmsc->scsi_volume[0].visible = 1;
	hmsc->scsi_volume[0].writable = 1;

	hmsc->scsi_volume[1].nr = 2;
	hmsc->scsi_volume[1].flags = HC_VOLUME_FLAG_VALID |
		HC_VOLUME_FLAG_ENCRYPTED |
		HC_VOLUME_FLAG_HIDDEN |
		HC_VOLUME_FLAG_VISIBLE_ON_UNLOCK;
	hmsc->scsi_volume[1].region_start = hmsc->scsi_volume[0].n_regions;
	hmsc->scsi_volume[1].n_regions = hmsc->scsi_num_regions - hmsc->scsi_volume[0].n_regions;
	hmsc->scsi_volume[1].started = 0;
	hmsc->scsi_volume[1].visible = 0;
	hmsc->scsi_volume[1].writable = 1;
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

	if(SCSI_IsReady(hmsc, lun) != 0) {
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

		while (len) {
			len--;
			hmsc->bot_data[len] = MSC_Page00_Inquiry_Data[len];
		}
	} else {
		pPage = SCSI_InquiryData + (0 * STANDARD_INQUIRY_DATA_LEN);
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

static u8 capacity_resp[8] __attribute__((aligned(16)));

static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if (SCSI_GetCapacity(hmsc, lun, &hmsc->scsi_blk_nbr, &hmsc->scsi_blk_size) != 0) {
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

	for(i = 0U; i < 12U ; i++) {
		bot_data[i] = 0U;
	}

	if(SCSI_GetCapacity(hmsc, lun, &blk_nbr, &blk_size) != 0U) {
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
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];

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

void readProcessingComplete(struct bufferFIFO *bf, u32 errorStatus, u32 errorStage)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	if (errorStatus == HAL_OK) {
		assert(s_mmc.DataToTransfer == 0);
		MSC_BOT_SendCSW (g_pdev, USBD_CSW_CMD_PASSED);
	} else {
		//TODO: Do we really need to reset bDataResidue?
		hmsc->csw.dDataResidue = hmsc->cbw.dDataLength;
		MSC_BOT_SendCSW (g_pdev, USBD_CSW_CMD_PASSED);
		MSC_BOT_Abort(g_pdev);
	}
}

static int8_t SCSI_ProcessRead (USBD_HandleTypeDef  *pdev, uint8_t lun)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];
	if (hmsc->usb_transmitting) {
		hmsc->usb_transmitting = 0;
		USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*)pdev->pClassData[INTERFACE_MSC];
		uint32_t len = MIN(hmsc->scsi_blk_len, hmsc->writeLen);
		hmsc->scsi_blk_addr += len;
		hmsc->scsi_blk_len -= len;
		hmsc->csw.dDataResidue -= len;
		if (hmsc->scsi_blk_len == 0) {
			bufferFIFO_stallStage(&hmsc->usbBulkBufferFIFO, hmsc->stageIdx);
		}
		bufferFIFO_processingComplete(&hmsc->usbBulkBufferFIFO, hmsc->stageIdx, len, 0, HAL_OK);
	}
	return 0;
}

static void processUSBReadBuffer(struct bufferFIFO *bf, int readSize, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	hmsc->usb_transmitting = 1;
	hmsc->stageIdx = stageIdx;
	hmsc->writeBuffer = bufferWrite;
	hmsc->writeLen = readSize;
	USBD_LL_Transmit(g_pdev, MSC_EPIN_ADDR, hmsc->writeBuffer, hmsc->writeLen);
}

static void processMMCReadBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	uint32_t len;
	if (s_mmc.DataToTransfer <= bf->maxBufferSize) {
		len = s_mmc.DataToTransfer;
	} else {
		len = bf->maxBufferSize;
	}
	s_mmc.BufferRead = bufferWrite;
	s_mmc.StageIdx = stageIdx;
	s_mmc.ReadLen = len;
	emmc_user_queue(EMMC_USER_STORAGE);
}

#ifdef BOOT_MODE_B

static void set_crypt_config()
{
	derive_iv(s_scsi_crypt.cur_aes_sector, s_scsi_crypt.cur_aes_iv);
	s_scsi_crypt.aes_crypt_conf.DataType = CRYP_DATATYPE_32B;
	s_scsi_crypt.aes_crypt_conf.KeySize = CRYP_KEYSIZE_128B;
	s_scsi_crypt.aes_crypt_conf.pKey = (u32 *)g_cmd_state.encrypt_key;
	s_scsi_crypt.aes_crypt_conf.pInitVect = (u32 *)s_scsi_crypt.cur_aes_iv;
	s_scsi_crypt.aes_crypt_conf.Algorithm = CRYP_AES_CBC;
	s_scsi_crypt.aes_crypt_conf.DataWidthUnit = CRYP_DATAWIDTHUNIT_WORD;
	HAL_CRYP_SetConfig(&g_hcryp, &s_scsi_crypt.aes_crypt_conf);
}

static void processDecryptReadBuffer(struct bufferFIFO *bf,
		int readLen, u32 readData,
		const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	s_scsi_crypt.stageIdx = stageIdx;
	s_scsi_crypt.txLen = readLen;
	assert((readLen % 512) == 0);
	s_scsi_crypt.num_aes_sector = readLen / 512;
	s_scsi_crypt.aes_read = (u32 *)bufferRead;
	s_scsi_crypt.aes_write = (u32 *)bufferWrite;
	s_scsi_crypt.aes_encrypt = 0;
	set_crypt_config();
	HAL_CRYP_Decrypt_DMA(&g_hcryp, s_scsi_crypt.aes_read, 512/4, s_scsi_crypt.aes_write);
}

#endif

void emmc_user_read_storage_rx_complete(MMC_HandleTypeDef *hmmc)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	u32 rc = hmmc->ErrorCode;
	if (rc == HAL_OK) {
		s_mmc.DataToTransfer -= s_mmc.ReadLen;
		s_mmc.DataTransferred += s_mmc.ReadLen;
		s_mmc.BlockAddr += s_mmc.ReadLen/512;
		s_mmc.BlocksToTransfer -= s_mmc.ReadLen/512;

		if (s_mmc.DataToTransfer == 0) {
			bufferFIFO_stallStage(&hmsc->usbBulkBufferFIFO, s_mmc.StageIdx);
		}
	}
	emmc_user_done();
	bufferFIFO_processingComplete(&hmsc->usbBulkBufferFIFO, s_mmc.StageIdx, s_mmc.ReadLen, 0, rc);
}

static int8_t SCSI_Read10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];
	if(hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 10 : Ho <> Di */
		if ((hmsc->cbw.bmFlags & 0x80U) != 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		if(SCSI_IsReady(hmsc, lun) != 0) {
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
		uint32_t len = MIN(hmsc->scsi_blk_len, hmsc->usbBulkBufferFIFO.maxBufferSize);
		s_mmc.DataToTransfer = hmsc->scsi_blk_len;
		s_mmc.BlocksToTransfer = blk_len;
		s_mmc.BlockAddr = blk_addr;
		s_mmc.DataTransferred = 0;
#ifdef BOOT_MODE_B
		if (hmsc->scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED) {
			s_scsi_crypt.dataToTransfer = hmsc->scsi_blk_len;
			s_scsi_crypt.cur_aes_sector = blk_addr;
			hmsc->usbBulkBufferFIFO.numStages = 3;
			hmsc->usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
			hmsc->usbBulkBufferFIFO.processStage[1] = processDecryptReadBuffer;
			hmsc->usbBulkBufferFIFO.processStage[2] = processUSBReadBuffer;
			hmsc->usbBulkBufferFIFO.processingComplete = readProcessingComplete;
		} else {
			hmsc->usbBulkBufferFIFO.numStages = 2;
			hmsc->usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
			hmsc->usbBulkBufferFIFO.processStage[1] = processUSBReadBuffer;
			hmsc->usbBulkBufferFIFO.processingComplete = readProcessingComplete;
		}
#else
		hmsc->usbBulkBufferFIFO.numStages = 2;
		hmsc->usbBulkBufferFIFO.processStage[0] = processMMCReadBuffer;
		hmsc->usbBulkBufferFIFO.processStage[1] = processUSBReadBuffer;
		hmsc->usbBulkBufferFIFO.processingComplete = readProcessingComplete;
#endif
		bufferFIFO_start(&hmsc->usbBulkBufferFIFO, len);
		return 0;
	} else {
		return SCSI_ProcessRead(pdev, lun);
	}
}

void emmc_user_storage_start()
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	HAL_MMC_CardStateTypeDef cardState;
	if (hmsc->bot_state == USBD_BOT_DATA_IN) {
		int lun = hmsc->cbw.bLUN;

		//TODO: Need timeout and error checking here
		do {
			cardState = HAL_MMC_GetCardState(&g_hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		u32 blockAddrAdj = s_mmc.BlockAddr +
			(EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ)) +
			(hmsc->scsi_volume[lun].region_start * hmsc->scsi_region_size_blocks);

		u32 rc = HAL_MMC_ReadBlocks_DMA(&g_hmmc1, s_mmc.BufferRead,
				blockAddrAdj,
				s_mmc.ReadLen/512);
		if (rc != HAL_OK) {
			if (!g_hmmc1.ErrorCode) {
				g_hmmc1.ErrorCode = rc;
			}
			emmc_user_read_storage_rx_complete(&g_hmmc1);
		}
	} else if (hmsc->bot_state == USBD_BOT_DATA_OUT) {
		int lun = hmsc->cbw.bLUN;

		//TODO: Need timeout and error checking here
		do {
			cardState = HAL_MMC_GetCardState(&g_hmmc1);
		} while (cardState != HAL_MMC_CARD_TRANSFER);

		u32 blockAddrAdj = s_mmc.BlockAddr +
			(EMMC_STORAGE_FIRST_BLOCK * (HC_BLOCK_SZ/EMMC_SUB_BLOCK_SZ)) +
			(hmsc->scsi_volume[lun].region_start * hmsc->scsi_region_size_blocks);

		u32 rc = HAL_MMC_WriteBlocks_DMA_Initial(&g_hmmc1, s_mmc.BufferWrite, s_mmc.ReadLen,
				blockAddrAdj,
				s_mmc.ReadLen/512);
		if (rc != HAL_OK) {
			if (!g_hmmc1.ErrorCode) {
				g_hmmc1.ErrorCode = rc;
			}
			emmc_user_write_storage_tx_complete(&g_hmmc1);
		}
	}
}

void writeProcessingComplete(struct bufferFIFO *bf, u32 errorStatus, u32 errorStage)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	if (errorStatus == 0) {
		assert(s_mmc.DataToTransfer == 0);
		MSC_BOT_SendCSW (g_pdev, USBD_CSW_CMD_PASSED);
	} else {
		//TODO: Do we really need to reset bDataResidue?
		hmsc->csw.dDataResidue = hmsc->cbw.dDataLength;
		MSC_BOT_SendCSW (g_pdev, USBD_CSW_CMD_PASSED);
		MSC_BOT_Abort(g_pdev);
	}
}

void processUSBWriteBuffer(struct bufferFIFO *bf, int readSize, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	uint32_t len;
	if (hmsc->scsi_blk_len <= bf->maxBufferSize) {
		len = hmsc->scsi_blk_len;
	} else {
		len = bf->maxBufferSize;
	}
	hmsc->stageIdx = stageIdx;
	hmsc->writeBuffer = bufferWrite;
	hmsc->writeLen = len;
	USBD_LL_PrepareReceive (g_pdev, MSC_EPOUT_ADDR, bufferWrite, len);
}

#ifdef BOOT_MODE_B

void HAL_CRYP_OutCpltCallback(CRYP_HandleTypeDef *hcryp)
{
	BEGIN_WORK(USBD_SCSI_CRYPT_WORK);
}

#endif

void usbd_scsi_idle()
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
#ifdef BOOT_MODE_B
	if (HAS_WORK(USBD_SCSI_CRYPT_WORK)) {
		END_WORK(USBD_SCSI_CRYPT_WORK);
		s_scsi_crypt.cur_aes_sector++;
		s_scsi_crypt.num_aes_sector--;
		s_scsi_crypt.aes_read += 512/4;
		s_scsi_crypt.aes_write += 512/4;
		if (!s_scsi_crypt.num_aes_sector) {
			s_scsi_crypt.dataToTransfer -= s_scsi_crypt.txLen;
			if (s_scsi_crypt.dataToTransfer == 0) {
				bufferFIFO_stallStage(&hmsc->usbBulkBufferFIFO, s_scsi_crypt.stageIdx);
			}
			u32 rc = g_hmmc1.ErrorCode;
			bufferFIFO_processingComplete(&hmsc->usbBulkBufferFIFO, s_scsi_crypt.stageIdx, s_scsi_crypt.txLen, 0, rc);
		} else {
			set_crypt_config();
			if (s_scsi_crypt.aes_encrypt) {
				HAL_CRYP_Encrypt_DMA(&g_hcryp, s_scsi_crypt.aes_read, 512/4, s_scsi_crypt.aes_write);
			} else {
				HAL_CRYP_Decrypt_DMA(&g_hcryp, s_scsi_crypt.aes_read, 512/4, s_scsi_crypt.aes_write);
			}
		}
	}
#endif
}

#ifdef BOOT_MODE_B
void processEncryptWriteBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	s_scsi_crypt.stageIdx = stageIdx;
	s_scsi_crypt.txLen = readLen;

	assert((readLen % 512) == 0);
	s_scsi_crypt.num_aes_sector = readLen / 512;
	s_scsi_crypt.aes_read = (u32 *)bufferRead;
	s_scsi_crypt.aes_write = (u32 *)bufferWrite;
	s_scsi_crypt.aes_encrypt = 1;
	set_crypt_config();
	HAL_CRYP_Encrypt_DMA(&g_hcryp, s_scsi_crypt.aes_read, 512/4, s_scsi_crypt.aes_write);
}
#endif

void processMMCWriteBuffer(struct bufferFIFO *bf, int readLen, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx)
{
	s_mmc.StageIdx = stageIdx;
	s_mmc.ReadLen = readLen;
	s_mmc.BufferWrite = bufferRead;
	emmc_user_queue(EMMC_USER_STORAGE);
}

void emmc_user_write_storage_tx_complete(MMC_HandleTypeDef *hmmc)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) g_pdev->pClassData[INTERFACE_MSC];
	u32 rc = hmmc->ErrorCode;
	if (rc == HAL_OK) {
		s_mmc.DataToTransfer -= s_mmc.ReadLen;
		s_mmc.DataTransferred += s_mmc.ReadLen;
		s_mmc.BlockAddr += s_mmc.ReadLen/512;
		s_mmc.BlocksToTransfer -= s_mmc.ReadLen/512;
		if (s_mmc.DataToTransfer == 0) {
			bufferFIFO_stallStage(&hmsc->usbBulkBufferFIFO, s_mmc.StageIdx);
		}
	}
	emmc_user_done();
	bufferFIFO_processingComplete(&hmsc->usbBulkBufferFIFO, s_mmc.StageIdx, s_mmc.ReadLen, 0, rc);
}

static int8_t SCSI_Write10 (USBD_HandleTypeDef  *pdev, uint8_t lun, uint8_t *params)
{
	USBD_MSC_BOT_HandleTypeDef  *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[INTERFACE_MSC];

	if (hmsc->bot_state == USBD_BOT_IDLE) { /* Idle */
		/* case 8 : Hi <> Do */
		if ((hmsc->cbw.bmFlags & 0x80U) == 0x80U) {
			SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		/* Check whether Media is ready */
		if(SCSI_IsReady(hmsc, lun) != 0) {
			SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT, 0);
			hmsc->bot_data_length = 0U;
			hmsc->bot_state = USBD_BOT_NO_DATA;
			return -1;
		}

		/* Check If media is write-protected */
		if(SCSI_IsWriteProtected(hmsc, lun) != 0) {
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
		uint32_t len = MIN(hmsc->scsi_blk_len, hmsc->usbBulkBufferFIFO.maxBufferSize);
		s_mmc.DataToTransfer = hmsc->scsi_blk_len;
		s_mmc.BlocksToTransfer = blk_len;
		s_mmc.BlockAddr = blk_addr;
		s_mmc.DataTransferred = 0;
#ifdef BOOT_MODE_B
		if (hmsc->scsi_volume[lun].flags & HC_VOLUME_FLAG_ENCRYPTED) {
			s_scsi_crypt.dataToTransfer = hmsc->scsi_blk_len;
			s_scsi_crypt.cur_aes_sector = blk_addr;
			hmsc->usbBulkBufferFIFO.numStages = 3;
			hmsc->usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
			hmsc->usbBulkBufferFIFO.processStage[1] = processEncryptWriteBuffer;
			hmsc->usbBulkBufferFIFO.processStage[2] = processMMCWriteBuffer;
			hmsc->usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
		} else {
			hmsc->usbBulkBufferFIFO.numStages = 2;
			hmsc->usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
			hmsc->usbBulkBufferFIFO.processStage[1] = processMMCWriteBuffer;
			hmsc->usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
		}
#else
		hmsc->usbBulkBufferFIFO.numStages = 2;
		hmsc->usbBulkBufferFIFO.processStage[0] = processUSBWriteBuffer;
		hmsc->usbBulkBufferFIFO.processStage[1] = processMMCWriteBuffer;
		hmsc->usbBulkBufferFIFO.processingComplete = writeProcessingComplete;
#endif
		bufferFIFO_start(&hmsc->usbBulkBufferFIFO, len);
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
		bufferFIFO_stallStage(&hmsc->usbBulkBufferFIFO, hmsc->stageIdx);
	}
	bufferFIFO_processingComplete(&hmsc->usbBulkBufferFIFO, hmsc->stageIdx, len, 0, HAL_OK);
	return 0;
}
