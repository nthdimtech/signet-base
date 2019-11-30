
#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "signetdev_common.h"
#include "usb_raw_hid.h"

#define LSB(X) ((X) & 0xff)
#define MSB(X) ((X) >> 8)
#define WTB(X) LSB(X), MSB(X)

static int endpointToInterface(uint8_t epNum)
{
	return ((int)(epNum & ~(0x80))) - 1;
}

static uint8_t interfaceToEndpointIn(int interfaceNum)
{
	return (uint8_t)((interfaceNum + 1) | 0x80);
}

static uint8_t interfaceToEndpointOut(int interfaceNum)
{
	return (uint8_t)(interfaceNum + 1);
}

static uint8_t  USBD_HID_Init (USBD_HandleTypeDef *pdev,
                               uint8_t cfgidx);

static uint8_t  USBD_HID_DeInit (USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx);

static uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);

static uint8_t  *USBD_HID_GetFSCfgDesc (uint16_t *length);

static uint8_t  *USBD_HID_GetHSCfgDesc (uint16_t *length);

static uint8_t  *USBD_HID_GetOtherSpeedCfgDesc (uint16_t *length);

static uint8_t  *USBD_HID_GetDeviceQualifierDesc (uint16_t *length);

static uint8_t  USBD_HID_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);

static uint8_t  USBD_HID_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);

USBD_ClassTypeDef  USBD_HID = {
	USBD_HID_Init,
	USBD_HID_DeInit,
	USBD_HID_Setup,
	NULL, /*EP0_TxSent*/
	NULL, /*EP0_RxReady*/
	USBD_HID_DataIn, /*DataIn*/
	USBD_HID_DataOut, /*DataOut*/
	NULL, /*SOF */
	NULL,
	NULL,
	USBD_HID_GetHSCfgDesc,
	USBD_HID_GetFSCfgDesc,
	USBD_HID_GetOtherSpeedCfgDesc,
	USBD_HID_GetDeviceQualifierDesc,
};

static const u8 cmd_hid_report_descriptor[] __attribute__((aligned (4))) = {
	0x06, LSB(USB_RAW_HID_USAGE_PAGE), MSB(USB_RAW_HID_USAGE_PAGE),
	0x0A, LSB(USB_RAW_HID_USAGE), MSB(USB_RAW_HID_USAGE),
	0xa1, 0x01,                             // Collection 0x01

	0x75, 0x08,                             // report size = 8 bits
	0x15, 0x00,                             // logical minimum = 0
	0x26, 0xFF, 0x00,                       // logical maximum = 255
	0x95, CMD_HID_TX_SIZE,                  // report count

	0x09, 0x01,                             // usage
	0x81, 0x02,                             // Input (array)
	0x95, CMD_HID_RX_SIZE,                  // report count
	0x09, 0x02,                             // usage
	0x91, 0x02,                             // Output (array)
	0xC0                                    // end collection
};

static const u8 fido_hid_report_descriptor[] __attribute__((aligned (4))) = {

	0x06, 0xd0, 0xf1,             // USAGE_PAGE (FIDO Alliance)
	0x09, 0x01,                   // USAGE (Keyboard)
	0xa1, 0x01,                   // COLLECTION (Application)

	0x09, 0x20,                   //   USAGE (Input Report Data)
	0x15, 0x00,                   //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,             //   LOGICAL_MAXIMUM (255)
	0x75, 0x08,                   //   REPORT_SIZE (8)
	0x95, FIDO_HID_TX_SIZE,       //   REPORT_COUNT (64)
	0x81, 0x02,                   //   INPUT (Data,Var,Abs)

	0x09, 0x21,                   //   USAGE(Output Report Data)
	0x15, 0x00,                   //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,             //   LOGICAL_MAXIMUM (255)
	0x75, 0x08,                   //   REPORT_SIZE (8)
	0x95, FIDO_HID_RX_SIZE,       //   REPORT_COUNT (64)
	0x91, 0x02,                   //   OUTPUT (Data,Var,Abs)

	0xc0,// END_COLLECTION                                  // end collection
};

/* USB HID device HS Configuration Descriptor */
static uint8_t USBD_HID_CfgHSDesc[USB_HID_CONFIG_DESC_SIZ] __attribute__((aligned (4))) = {
	0x09, /* bLength: Configuration Descriptor size */
	USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType: Configuration */
	USB_HID_CONFIG_DESC_SIZ, /* wTotalLength: Bytes returned */
	0x00,
	0x02,         /*bNumInterfaces: 1 interface*/
	0x01,         /*bConfigurationValue: Configuration value*/
	0x00,         /*iConfiguration: Index of string descriptor describing the configuration*/
	0xE0,         /*bmAttributes: bus powered and Support Remote Wake-up */
	0x32,         /*MaxPower 100 mA: this current is used for detecting Vbus*/


	/************** Descriptor of Command RAW HID interface  ****************/
	0x09,         /*bLength: Interface Descriptor size*/
	USB_DESC_TYPE_INTERFACE,/*bDescriptorType: Interface descriptor type*/
	INTERFACE_CMD,         /*bInterfaceNumber: Number of Interface*/
	0x00,         /*bAlternateSetting: Alternate setting*/
	0x02,         /*bNumEndpoints*/
	0x03,         /*bInterfaceClass: HID*/
	0x01,         /*bInterfaceSubClass : 1=BOOT, 0=no boot*/
	0x00,         /*nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse*/
	0,            /*iInterface: Index of string descriptor*/
	/******************** Descriptor of Command RAW HID ********************/
	0x09,         /*bLength: HID Descriptor size*/
	HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
	0x11,         /*bcdHID: HID Class Spec release number*/
	0x01,
	0x00,         /*bCountryCode: Hardware target country*/
	0x01,         /*bNumDescriptors: Number of HID class descriptors to follow*/
	0x22,         /*bDescriptorType*/
	sizeof(cmd_hid_report_descriptor),/*wItemLength: Total length of Report descriptor*/
	0x00,
	/******************** Descriptor of Command HID IN endpoint ********************/
	0x07,          /*bLength: Endpoint Descriptor size*/
	USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
	HID_CMD_EPIN_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
	0x03,          /*bmAttributes: Interrupt endpoint*/
	HID_CMD_EPIN_SIZE,
	0x00,
	HID_HS_BINTERVAL,          /*bInterval: Polling Interval */
	/******************** Descriptor of Command HID OUT endpoint ********************/
	0x07,          /*bLength: Endpoint Descriptor size*/
	USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
	HID_CMD_EPOUT_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
	0x03,          /*bmAttributes: Interrupt endpoint*/
	HID_CMD_EPOUT_SIZE,
	0x00,
	HID_HS_BINTERVAL,          /*bInterval: Polling Interval */

	/************** Descriptor of FIDO HID interface  ****************/
	0x09,         /*bLength: Interface Descriptor size*/
	USB_DESC_TYPE_INTERFACE,/*bDescriptorType: Interface descriptor type*/
	INTERFACE_FIDO,         /*bInterfaceNumber: Number of Interface*/
	0x00,         /*bAlternateSetting: Alternate setting*/
	0x02,         /*bNumEndpoints*/
	0x03,         /*bInterfaceClass: HID*/
	0x01,         /*bInterfaceSubClass : 1=BOOT, 0=no boot*/
	0x00,         /*nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse*/
	0,            /*iInterface: Index of string descriptor*/
	/******************** Descriptor of FIDO HID ********************/
	0x09,         /*bLength: HID Descriptor size*/
	HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
	0x11,         /*bcdHID: HID Class Spec release number*/
	0x01,         //TODO
	0x00,         /*bCountryCode: Hardware target country*/
	0x01,         /*bNumDescriptors: Number of HID class descriptors to follow*/
	0x22,         /*bDescriptorType*/
	sizeof(fido_hid_report_descriptor),/*wItemLength: Total length of Report descriptor*/
	0x00,
	/******************** Descriptor of FIDO HID IN endpoint ********************/
	0x07,          /*bLength: Endpoint Descriptor size*/
	USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
	HID_FIDO_EPIN_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
	0x03,          /*bmAttributes: Interrupt endpoint*/
	HID_FIDO_EPIN_SIZE,
	0x00,
	HID_HS_BINTERVAL,          /*bInterval: Polling Interval */
	/******************** Descriptor of FIDO HID OUT endpoint ********************/
	0x07,          /*bLength: Endpoint Descriptor size*/
	USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
	HID_FIDO_EPOUT_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
	0x03,          /*bmAttributes: Interrupt endpoint*/
	HID_FIDO_EPOUT_SIZE,
	0x00,
	HID_HS_BINTERVAL,          /*bInterval: Polling Interval */
};

/* USB HID device Configuration Descriptor */
static uint8_t USBD_HID_Cmd_Desc[USB_HID_DESC_SIZ] __attribute__((aligned (4))) = {
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

static uint8_t USBD_HID_FIDO_Desc[USB_HID_DESC_SIZ] __attribute__((aligned (4))) = {
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

/* USB Standard Device Descriptor */
static uint8_t USBD_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __attribute__((aligned (4))) = {
	USB_LEN_DEV_QUALIFIER_DESC,
	USB_DESC_TYPE_DEVICE_QUALIFIER,
	0x00,
	0x02,
	0x00,
	0x00,
	0x00,
	0x40,
	0x01,
	0x00,
};

static USBD_HID_HandleTypeDef s_cmdHIDClassData;
static USBD_HID_HandleTypeDef s_fidoHIDClassData;

static uint8_t  USBD_HID_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	/* Open EP IN */
	USBD_LL_OpenEP(pdev, HID_CMD_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_CMD_EPIN_SIZE);
	pdev->ep_in[HID_CMD_EPIN_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, HID_FIDO_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPIN_SIZE);
	pdev->ep_in[HID_FIDO_EPIN_ADDR & 0xFU].is_used = 1U;

	pdev->pClassData[0] = &s_cmdHIDClassData;
	pdev->pClassData[1] = &s_fidoHIDClassData;

	USBD_LL_OpenEP(pdev, HID_CMD_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_CMD_EPOUT_SIZE);
	pdev->ep_in[HID_CMD_EPOUT_ADDR & 0xFU].is_used = 1U;
	USBD_LL_PrepareReceive (pdev, HID_CMD_EPOUT_ADDR, s_cmdHIDClassData.rx_buffer, HID_CMD_EPOUT_SIZE);
	s_cmdHIDClassData.state = HID_IDLE;

	USBD_LL_OpenEP(pdev, HID_FIDO_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPOUT_SIZE);
	pdev->ep_in[HID_FIDO_EPOUT_ADDR & 0xFU].is_used = 1U;
	USBD_LL_PrepareReceive (pdev, HID_FIDO_EPOUT_ADDR, s_fidoHIDClassData.rx_buffer, HID_FIDO_EPOUT_SIZE);
	s_fidoHIDClassData.state = HID_IDLE;

	return USBD_OK;
}

/**
  * @brief  USBD_HID_Init
  *         DeInitialize the HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_HID_DeInit (USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx)
{
	/* Close HID EPs */
	USBD_LL_CloseEP(pdev, HID_CMD_EPIN_ADDR);
	pdev->ep_in[HID_CMD_EPIN_ADDR & 0xFU].is_used = 0U;
	USBD_LL_CloseEP(pdev, HID_CMD_EPOUT_ADDR);
	pdev->ep_out[HID_CMD_EPOUT_ADDR & 0xFU].is_used = 0U;

	for (int i = 0; i < 5; i++) {
		if(pdev->pClassData[i] != NULL) {
			//USBD_free(pdev->pClassData[0]); //NEN_TODO
			pdev->pClassData[i] = NULL;
		}
	}

	return USBD_OK;
}

/**
  * @brief  USBD_HID_Setup
  *         Handle the HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
	USBD_HID_HandleTypeDef *hhid = NULL;
	uint16_t len = 0U;
	uint8_t *pbuf = NULL;
	uint16_t status_info = 0U;
	USBD_StatusTypeDef ret = USBD_OK;

	if ((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE) {
		hhid = (USBD_HID_HandleTypeDef*) pdev->pClassData[req->wIndex];
	}

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
		case USB_REQ_GET_STATUS:
			if (pdev->dev_state == USBD_STATE_CONFIGURED) {
				USBD_CtlSendData (pdev, (uint8_t *)(void *)&status_info, 2U);
			} else {
				USBD_CtlError (pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_GET_DESCRIPTOR:
			if(req->wValue >> 8 == HID_REPORT_DESC) {
				if (req->wIndex == 0) {
					len = MIN(sizeof(cmd_hid_report_descriptor), req->wLength);
					pbuf = cmd_hid_report_descriptor;
				} else {
					len = MIN(sizeof(fido_hid_report_descriptor), req->wLength);
					pbuf = fido_hid_report_descriptor;
				}
			} else if(req->wValue >> 8 == HID_DESCRIPTOR_TYPE) {
				if (req->wIndex == 0) {
					pbuf = USBD_HID_Cmd_Desc;
					len = MIN(USB_HID_DESC_SIZ, req->wLength);
				} else {
					pbuf = USBD_HID_FIDO_Desc;
					len = MIN(USB_HID_DESC_SIZ, req->wLength);
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

/**
  * @brief  USBD_HID_SendReport
  *         Send HID Report
  * @param  pdev: device instance
  * @param  buff: pointer to report
  * @retval status
  */
uint8_t USBD_HID_SendReport     (USBD_HandleTypeDef  *pdev,
                                 int interfaceNum,
                                 uint8_t *report,
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

/**
  * @brief  USBD_HID_GetCfgFSDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t  *USBD_HID_GetFSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

/**
  * @brief  USBD_HID_GetCfgHSDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t  *USBD_HID_GetHSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

/**
  * @brief  USBD_HID_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t  *USBD_HID_GetOtherSpeedCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

/**
  * @brief  USBD_HID_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */

USBD_HandleTypeDef *s_pdev = NULL;

static uint8_t  USBD_HID_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	/* Ensure that the FIFO is empty before a new transfer, this condition could
	be caused by  a new transfer before the end of the previous transfer */
	int interfaceNum = endpointToInterface(epnum);
	switch (interfaceNum) {
	case INTERFACE_CMD:
	case INTERFACE_FIDO: {
		USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)s_pdev->pClassData[interfaceNum]);
		hhid->state = HID_IDLE;
		if (hhid->tx_report) {
			USBD_LL_Transmit(s_pdev, interfaceToEndpointIn(interfaceNum), hhid->tx_report, 64);
			hhid->tx_report = NULL;
		}
		if (interfaceNum == INTERFACE_CMD) {
			usb_raw_hid_tx();
		} else {
			//NEN_TODO
		}
	} break;
	default:
		//NEN_TODO
		break;
	}
	return USBD_OK;
}

void usb_fido_hid_rx(uint8_t *buffer, int len)
{
	//NEN_TODO
}

static uint8_t  USBD_HID_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	s_pdev = pdev;
	int interfaceNum = endpointToInterface(epnum);
	switch (interfaceNum) {
	case INTERFACE_CMD:
#ifdef ENABLE_FIDO2
	case INTERFACE_FIDO:
#endif
	{
		USBD_HID_HandleTypeDef *hhid = ((USBD_HID_HandleTypeDef *)pdev->pClassData[interfaceNum]);
		uint8_t *rx_buffer = hhid->rx_buffer;
		if (interfaceNum == INTERFACE_CMD) {
			usb_raw_hid_rx(rx_buffer, CMD_HID_RX_SIZE);
		}
#ifdef ENABLE_FIDO2
		else {
			ctaphid_handle_packet(rx_buffer);
		}
#endif
		USBD_LL_PrepareReceive (pdev, epnum, rx_buffer, 64);
	}
	break;
	default:
		//NEN_TODO: Gnuk and mass storage
		break;
	}
	return USBD_OK;
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
		//NEN_TODO: Gnuk and mass storage
		break;
	}
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
		return 0; //NEN_TODO
		break;
	}
}
/**
* @brief  DeviceQualifierDescriptor
*         return Device Qualifier descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
static uint8_t  *USBD_HID_GetDeviceQualifierDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_DeviceQualifierDesc);
	return USBD_HID_DeviceQualifierDesc;
}
