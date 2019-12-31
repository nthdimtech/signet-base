#include "usbd_hid.h"
#include "usbd_multi.h"
#include "signetdev_common.h"
#ifdef ENABLE_FIDO2
#include "fido2/ctaphid.h"
#endif
#include "usb_raw_hid.h"

#define LSB(X) ((X) & 0xff)
#define MSB(X) ((X) >> 8)
#define WTB(X) LSB(X), MSB(X)

extern USBD_HandleTypeDef *s_pdev;

//
// Warning: If you change this structure you must also change it in usbd_multi.c
// 
static const u8 cmd_hid_report_descriptor[] __attribute__((aligned (4))) = {
	0x06, LSB(USB_RAW_HID_USAGE_PAGE), MSB(USB_RAW_HID_USAGE_PAGE),
	0x0A, LSB(USB_RAW_HID_USAGE), MSB(USB_RAW_HID_USAGE),
	0xa1, 0x01,                             // Collection 0x01

	0x75, 0x08,                             // report size = 8 bits
	0x15, 0x00,                             // logical minimum = 0
	0x26, 0xFF, 0x00,                       // logical maximum = 255
	0x96, LSB(HID_CMD_EPIN_SIZE), MSB(HID_CMD_EPIN_SIZE), // report count

	0x09, 0x01,                             // usage
	0x81, 0x02,                             // Input (array)
	0x96, LSB(HID_CMD_EPOUT_SIZE), MSB(HID_CMD_EPOUT_SIZE), // report count
	0x09, 0x02,                             // usage
	0x91, 0x02,                             // Output (array)
	0xC0                                    // end collection
};

//
// Warning: If you change this structure you must also change it in usbd_multi.c
// 
static const u8 fido_hid_report_descriptor[] __attribute__((aligned (4))) = {

	0x06, 0xd0, 0xf1,             // USAGE_PAGE (FIDO Alliance)
	0x09, 0x01,                   // USAGE (Keyboard)
	0xa1, 0x01,                   // COLLECTION (Application)

	0x09, 0x20,                   //   USAGE (Input Report Data)
	0x15, 0x00,                   //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,             //   LOGICAL_MAXIMUM (255)
	0x75, 0x08,                   //   REPORT_SIZE (8)
	0x95, HID_FIDO_EPIN_SIZE,       //   REPORT_COUNT (64)
	0x81, 0x02,                   //   INPUT (Data,Var,Abs)

	0x09, 0x21,                   //   USAGE(Output Report Data)
	0x15, 0x00,                   //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,             //   LOGICAL_MAXIMUM (255)
	0x75, 0x08,                   //   REPORT_SIZE (8)
	0x95, HID_FIDO_EPOUT_SIZE,       //   REPORT_COUNT (64)
	0x91, 0x02,                   //   OUTPUT (Data,Var,Abs)

	0xc0,// END_COLLECTION                                  // end collection
};

/* USB HID device Configuration Descriptor */
static const uint8_t USBD_HID_Cmd_Desc[USB_HID_DESC_SIZ] __attribute__((aligned (4))) = {
	0x09,         /*bLength: HID Descriptor size*/
	HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
	0x11,         /*bcdHID: HID Class Spec release number*/
	0x01,
	0x00,         /*bCountryCode: Hardware target country*/
	0x01,         /*bNumDescriptors: Number of HID class descriptors to follow*/
	0x22,         /*bDescriptorType*/
	sizeof(cmd_hid_report_descriptor),/*wItemLength: Total length of Report descriptor*/
	0x00,
};

static const uint8_t USBD_HID_FIDO_Desc[USB_HID_DESC_SIZ] __attribute__((aligned (4))) = {
	0x09,         /*bLength: HID Descriptor size*/
	HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
	0x11,         /*bcdHID: HID Class Spec release number*/
	0x01,         //TODO
	0x00,         /*bCountryCode: Hardware target country*/
	0x01,         /*bNumDescriptors: Number of HID class descriptors to follow*/
	0x22,         /*bDescriptorType*/
	sizeof(fido_hid_report_descriptor),/*wItemLength: Total length of Report descriptor*/
	0x00,
};

uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
	uint16_t len = 0U;
	const uint8_t *pbuf = NULL;
	USBD_StatusTypeDef ret = USBD_OK;
	int iface = req->wIndex;
	USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef*) pdev->pClassData[iface];
	switch (req->bmRequest & USB_REQ_TYPE_MASK) {
	case USB_REQ_TYPE_CLASS:
		switch (req->bRequest) {
		case HID_REQ_SET_PROTOCOL:
			hhid->Protocol = (uint8_t)(req->wValue);
			break;

		case HID_REQ_GET_PROTOCOL:
			USBD_CtlSendData (pdev, (uint8_t *)(void *)&hhid->Protocol, 1U);
			break;

		case HID_REQ_SET_IDLE:
			hhid->IdleState = (uint8_t)(req->wValue >> 8);
			break;

		case HID_REQ_GET_IDLE:
			USBD_CtlSendData (pdev, (uint8_t *)(void *)&hhid->IdleState, 1U);
			break;

		default:
			USBD_CtlError (pdev, req);
			ret = USBD_FAIL;
			break;
		}
		break;
	case USB_REQ_TYPE_STANDARD:
		switch (req->bRequest) {
		case USB_REQ_GET_DESCRIPTOR:
			if(req->wValue >> 8 == HID_REPORT_DESC) {
				switch(iface) {
				case INTERFACE_CMD:
					len = MIN(sizeof(cmd_hid_report_descriptor), req->wLength);
					pbuf = cmd_hid_report_descriptor;
					break;
				case INTERFACE_FIDO:
					len = MIN(sizeof(fido_hid_report_descriptor), req->wLength);
					pbuf = fido_hid_report_descriptor;
					break;
				}
			} else if(req->wValue >> 8 == HID_DESCRIPTOR_TYPE) {
				switch(iface) {
				case INTERFACE_CMD:
					pbuf = USBD_HID_Cmd_Desc;
					len = MIN(USB_HID_DESC_SIZ, req->wLength);
					break;
				case INTERFACE_FIDO:
					pbuf = USBD_HID_FIDO_Desc;
					len = MIN(USB_HID_DESC_SIZ, req->wLength);
					break;
				}
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
				break;
			}
			USBD_CtlSendData (pdev, pbuf, len);
			break;

		case USB_REQ_GET_INTERFACE :
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				USBD_CtlSendData (pdev, (uint8_t *)(void *)&hhid->AltSetting, 1U);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_SET_INTERFACE :
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				hhid->AltSetting = (uint8_t)(req->wValue);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
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

void USBD_HID_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	int interfaceNum = endpointToInterface(epnum);
	USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)pdev->pClassData[interfaceNum]);
	uint8_t *rx_buffer = hhid->rx_buffer;
	if (interfaceNum == INTERFACE_CMD) {
		usb_raw_hid_rx(rx_buffer, HID_CMD_EPIN_SIZE);
	}
#ifdef ENABLE_FIDO2
	else {
		ctaphid_handle_packet(rx_buffer);
	}
#endif
	USBD_LL_PrepareReceive (pdev, epnum, rx_buffer, hhid->packetSize);
}

void USBD_HID_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	int interfaceNum = endpointToInterface(epnum);
	switch (interfaceNum) {
	case INTERFACE_CMD:
	case INTERFACE_FIDO: {
		USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)s_pdev->pClassData[interfaceNum]);
		hhid->state = HID_IDLE;
		if (hhid->tx_report) {
			USBD_HID_SendReport(s_pdev, interfaceNum, hhid->tx_report, HID_CMD_EPIN_SIZE);
			hhid->tx_report = NULL;
		}
		if (interfaceNum == INTERFACE_CMD) {
			usb_raw_hid_tx();
		} else {
			//NEN_TODO: Fido transmit
		}
	
	} break;
	default:
		break;
	}
}

uint8_t USBD_HID_SendReport     (USBD_HandleTypeDef  *pdev,
                                 int interfaceNum,
                                 const uint8_t *report,
                                 uint16_t len)
{
	USBD_HID_HandleTypeDef     *hhid = (USBD_HID_HandleTypeDef*)pdev->pClassData[interfaceNum];

	if (pdev->dev_state == USBD_STATE_CONFIGURED ) {
		if(hhid->state == HID_IDLE) {
			hhid->state = HID_BUSY;
			USBD_LL_Transmit (pdev,
			                  interfaceToEndpointIn(interfaceNum),
			                  report,
			                  len);
		}
	}
	return USBD_OK;
}

/**
  * @brief  USBD_HID_GetPollingInterval
  *         return polling interval from endpoint descriptor
  * @param  pdev: device instance
  * @retval polling interval
  */
uint32_t USBD_HID_GetPollingInterval (USBD_HandleTypeDef *pdev)
{
	uint32_t polling_interval = 0U;

	/* HIGH-speed endpoints */
	if(pdev->dev_speed == USBD_SPEED_HIGH) {
		/* Sets the data transfer polling interval for high speed transfers.
		 Values between 1..16 are allowed. Values correspond to interval
		 of 2 ^ (bInterval-1). This option (8 ms, corresponds to HID_HS_BINTERVAL */
		polling_interval = (((1U <<(HID_HS_BINTERVAL - 1U))) / 8U);
	} else { /* LOW and FULL-speed endpoints */
		/* Sets the data transfer polling interval for low and full
		speed transfers */
		polling_interval =  HID_FS_BINTERVAL;
	}

	return ((uint32_t)(polling_interval));
}

int usb_tx_pending(int ep)
{
	int interfaceNum = endpointToInterface(ep);
	switch (interfaceNum) {
	case INTERFACE_CMD:
	case INTERFACE_FIDO: {
		USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)s_pdev->pClassData[interfaceNum]);
		return hhid->state != HID_IDLE;
	}
	break;
	default:
		return 0;
		break;
	}
}

void usb_send_bytes(int ep, const u8 *data, int length)
{
	int interfaceNum = endpointToInterface(ep);
	switch (interfaceNum) {
	case INTERFACE_CMD:
	case INTERFACE_FIDO: {
		USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)s_pdev->pClassData[interfaceNum]);
		if (usb_tx_pending(ep)) {
			hhid->tx_report = data;
		} else {
			int interfaceNum = endpointToInterface(ep);
			USBD_HID_SendReport(s_pdev, interfaceNum, data, length);
		}
	}
	break;
	default:
		//NEN_TODO: What are we supposed to do if the interface is invalid?
		break;
	}
}
