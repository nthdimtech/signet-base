
#include "usbd_msc.h"

uint8_t  USBD_MSC_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
	USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData[req->wIndex];
	uint8_t ret = USBD_OK;
	uint16_t status_info = 0U;

	switch (req->bmRequest & USB_REQ_TYPE_MASK) {

	/* Class request */
	case USB_REQ_TYPE_CLASS:
		switch (req->bRequest) {
		case BOT_GET_MAX_LUN:
			if((req->wValue  == 0U) && (req->wLength == 1U) &&
			    ((req->bmRequest & 0x80U) == 0x80U)) {
				hmsc->max_lun = MAX_SCSI_VOLUMES - 1;
				USBD_CtlSendData (pdev, (uint8_t *)(void *)&hmsc->max_lun, 1U);
			} else {
				USBD_CtlError(pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case BOT_RESET :
			if((req->wValue  == 0U) && (req->wLength == 0U) &&
			    ((req->bmRequest & 0x80U) != 0x80U)) {
				MSC_BOT_Reset(pdev);
			} else {
				USBD_CtlError(pdev, req);
				ret = USBD_FAIL;
			}
			break;

		default:
			USBD_CtlError(pdev, req);
			ret = USBD_FAIL;
			break;
		}
		break;
	/* Interface & Endpoint request */
	case USB_REQ_TYPE_STANDARD:
		switch (req->bRequest) {
		case USB_REQ_GET_STATUS:
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				USBD_CtlSendData (pdev, (uint8_t *)(void *)&status_info, 2U);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_GET_INTERFACE:
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				USBD_CtlSendData (pdev, (uint8_t *)(void *)&hmsc->interface, 1U);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_SET_INTERFACE:
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				hmsc->interface = (uint8_t)(req->wValue);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_CLEAR_FEATURE:

			/* Flush the FIFO and Clear the stall status */
			USBD_LL_FlushEP(pdev, (uint8_t)req->wIndex);

			/* Reactivate the EP */
			USBD_LL_CloseEP (pdev, (uint8_t)req->wIndex);
			if((((uint8_t)req->wIndex) & 0x80U) == 0x80U) {
				pdev->ep_in[(uint8_t)req->wIndex & 0xFU].is_used = 0U;
				if(pdev->dev_speed == USBD_SPEED_HIGH) {
					/* Open EP IN */
					USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK,
					               MSC_MAX_HS_PACKET);
				} else {
					/* Open EP IN */
					USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK,
					               MSC_MAX_FS_PACKET);
				}
				pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 1U;
			} else {
				pdev->ep_out[(uint8_t)req->wIndex & 0xFU].is_used = 0U;
				if(pdev->dev_speed == USBD_SPEED_HIGH) {
					/* Open EP OUT */
					USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK,
					               MSC_MAX_HS_PACKET);
				} else {
					/* Open EP OUT */
					USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK,
					               MSC_MAX_FS_PACKET);
				}
				pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 1U;
			}

			/* Handle BOT error */
			MSC_BOT_CplClrFeature(pdev, (uint8_t)req->wIndex);
			break;

		default:
			USBD_CtlError (pdev, req);
			ret = USBD_FAIL;
			break;
		}
		break;

	default:
		USBD_CtlError (pdev, req);
		ret = USBD_FAIL;
		break;
	}

	return ret;
}

uint8_t  USBD_MSC_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	MSC_BOT_DataIn(pdev, epnum);
	return USBD_OK;
}

uint8_t  USBD_MSC_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	MSC_BOT_DataOut(pdev, epnum);
	return USBD_OK;
}
