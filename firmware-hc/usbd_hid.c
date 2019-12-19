
#include "usbd_hid.h"
#include "usbd_msc.h"
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

static uint8_t  USBD_Multi_Init (USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx);

static uint8_t  USBD_Multi_DeInit (USBD_HandleTypeDef *pdev,
                                   uint8_t cfgidx);

static uint8_t  USBD_Multi_Setup (USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req);

static uint8_t  *USBD_Multi_GetFSCfgDesc (uint16_t *length);

static uint8_t  *USBD_Multi_GetHSCfgDesc (uint16_t *length);

static uint8_t  *USBD_Multi_GetOtherSpeedCfgDesc (uint16_t *length);

static uint8_t  *USBD_Multi_GetDeviceQualifierDesc (uint16_t *length);

static uint8_t  USBD_Multi_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t  USBD_Multi_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);

USBD_ClassTypeDef  USBD_Multi = {
	USBD_Multi_Init,
	USBD_Multi_DeInit,
	USBD_Multi_Setup,
	NULL, /*EP0_TxSent*/
	NULL, /*EP0_RxReady*/
	USBD_Multi_DataIn, /*DataIn*/
	USBD_Multi_DataOut, /*DataOut*/
	NULL, /*SOF */
	NULL,
	NULL,
	USBD_Multi_GetHSCfgDesc,
	USBD_Multi_GetFSCfgDesc,
	USBD_Multi_GetOtherSpeedCfgDesc,
	USBD_Multi_GetDeviceQualifierDesc,
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
	0x03,         /*bNumInterfaces: 3 interface*/
	0x01,         /*bConfigurationValue: Configuration value*/
	0x00,         /*iConfiguration: Index of string descriptor describing the configuration*/
	0xE0,         /*bmAttributes: bus powered and Support Remote Wake-up */
	0x32,         /*MaxPower 100 mA: this current is used for detecting Vbus*/

	/********************  Mass Storage interface ********************/
	0x09,   /* bLength: Interface Descriptor size */
	0x04,   /* bDescriptorType: */
	INTERFACE_MSC,   /* bInterfaceNumber: Number of Interface */
	0x00,   /* bAlternateSetting: Alternate setting */
	0x02,   /* bNumEndpoints*/
	0x08,   /* bInterfaceClass: MSC Class */
	0x06,   /* bInterfaceSubClass : SCSI transparent*/
	0x50,   /* nInterfaceProtocol */
	0x05,          /* iInterface: */
	/********************  Mass Storage Endpoints ********************/
	0x07,   /*Endpoint descriptor length = 7*/
	0x05,   /*Endpoint descriptor type */
	MSC_EPIN_ADDR,   /*Endpoint address (IN, address 1) */
	0x02,   /*Bulk endpoint type */
	LOBYTE(MSC_MAX_HS_PACKET),
	HIBYTE(MSC_MAX_HS_PACKET),
	0x00,   /*Polling interval in milliseconds */

	0x07,   /*Endpoint descriptor length = 7 */
	0x05,   /*Endpoint descriptor type */
	MSC_EPOUT_ADDR,   /*Endpoint address (OUT, address 1) */
	0x02,   /*Bulk endpoint type */
	LOBYTE(MSC_MAX_HS_PACKET),
	HIBYTE(MSC_MAX_HS_PACKET),
	0x00,    /*Polling interval in milliseconds*/

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
static uint8_t USBD_Multi_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __attribute__((aligned (4))) = {
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

static USBD_HID_HandleTypeDef s_cmdHIDClassData __attribute__((aligned(16)));
static USBD_HID_HandleTypeDef s_fidoHIDClassData __attribute__((aligned(16)));
static USBD_MSC_BOT_HandleTypeDef s_SCSIMSCClassData __attribute__((aligned(16)));

static uint8_t  USBD_Multi_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	/* Open EP IN */
	pdev->pClassData[INTERFACE_MSC] = &s_SCSIMSCClassData;
	USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_HS_PACKET);
	pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_HS_PACKET);
	pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 1U;
	MSC_BOT_Init(pdev);

	pdev->pClassData[INTERFACE_CMD] = &s_cmdHIDClassData;
	USBD_LL_OpenEP(pdev, HID_CMD_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_CMD_EPIN_SIZE);
	pdev->ep_in[HID_CMD_EPIN_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, HID_CMD_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_CMD_EPOUT_SIZE);
	pdev->ep_in[HID_CMD_EPOUT_ADDR & 0xFU].is_used = 1U;
	USBD_LL_PrepareReceive (pdev, HID_CMD_EPOUT_ADDR, s_cmdHIDClassData.rx_buffer, HID_CMD_EPOUT_SIZE);
	s_cmdHIDClassData.state = HID_IDLE;

	pdev->pClassData[INTERFACE_FIDO] = &s_fidoHIDClassData;
	USBD_LL_OpenEP(pdev, HID_FIDO_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPIN_SIZE);
	pdev->ep_in[HID_FIDO_EPIN_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, HID_FIDO_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPOUT_SIZE);
	pdev->ep_in[HID_FIDO_EPOUT_ADDR & 0xFU].is_used = 1U;
	USBD_LL_PrepareReceive (pdev, HID_FIDO_EPOUT_ADDR, s_fidoHIDClassData.rx_buffer, HID_FIDO_EPOUT_SIZE);
	s_fidoHIDClassData.state = HID_IDLE;
	return USBD_OK;
}

static uint8_t  USBD_Multi_DeInit (USBD_HandleTypeDef *pdev,
                                   uint8_t cfgidx)
{
	/* Close MSC EPs */
	USBD_LL_CloseEP(pdev, MSC_EPIN_ADDR);
	pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 0U;
	USBD_LL_CloseEP(pdev, MSC_EPOUT_ADDR);
	pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 0U;
	MSC_BOT_DeInit(pdev);

	/* Close CMD HID EPs */
	USBD_LL_CloseEP(pdev, HID_CMD_EPIN_ADDR);
	pdev->ep_in[HID_CMD_EPIN_ADDR & 0xFU].is_used = 0U;
	USBD_LL_CloseEP(pdev, HID_CMD_EPOUT_ADDR);
	pdev->ep_out[HID_CMD_EPOUT_ADDR & 0xFU].is_used = 0U;

	/* Close FIDO HID EPs */
	USBD_LL_CloseEP(pdev, HID_FIDO_EPIN_ADDR);
	pdev->ep_in[HID_FIDO_EPIN_ADDR & 0xFU].is_used = 0U;
	USBD_LL_CloseEP(pdev, HID_FIDO_EPOUT_ADDR);
	pdev->ep_out[HID_FIDO_EPOUT_ADDR & 0xFU].is_used = 0U;

	for (int i = 0; i < 5; i++) {
		if(pdev->pClassData[i] != NULL) {
			pdev->pClassData[i] = NULL;
		}
	}

	return USBD_OK;
}

static uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);

static uint8_t  USBD_Multi_Setup_Device(USBD_HandleTypeDef *pdev,
                                        USBD_SetupReqTypedef *req);

static uint8_t  USBD_Multi_Setup (USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req)
{
	USBD_StatusTypeDef ret = USBD_OK;

	if ((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE) {
		switch (req->wIndex) {
		case INTERFACE_MSC:
			return USBD_MSC_Setup(pdev, req);
			break;
		case INTERFACE_CMD:
		case INTERFACE_FIDO:
			return USBD_HID_Setup(pdev, req);
			break;
		default:
			//NEN_TODO
			break;
		}
	} else {
		return USBD_Multi_Setup_Device(pdev, req);
	}
	return ret;
}

static uint8_t  USBD_Multi_Setup_Device(USBD_HandleTypeDef *pdev,
                                        USBD_SetupReqTypedef *req)
{
	USBD_StatusTypeDef ret = USBD_OK;
	uint16_t status_info = 0U;
	switch (req->bmRequest & USB_REQ_TYPE_MASK) {
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

static uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
	uint16_t len = 0U;
	uint8_t *pbuf = NULL;
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

static uint8_t  *USBD_Multi_GetFSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

static uint8_t  *USBD_Multi_GetHSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

static uint8_t  *USBD_Multi_GetOtherSpeedCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_HID_CfgHSDesc);
	return USBD_HID_CfgHSDesc;
}

USBD_HandleTypeDef *s_pdev = NULL;

static uint8_t  USBD_Multi_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	/* Ensure that the FIFO is empty before a new transfer, this condition could
	be caused by  a new transfer before the end of the previous transfer */
	int interfaceNum = endpointToInterface(epnum);
	switch (interfaceNum) {
	case INTERFACE_MSC: {
		USBD_MSC_DataIn(pdev, epnum);
	}
	break;
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
			//NEN_TODO: Fido transmit
		}
	} break;
	default:
		//NEN_TODO: What are we supposed to return on invalid interface?
		break;
	}
	return USBD_OK;
}

static uint8_t  USBD_Multi_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	s_pdev = pdev;
	int interfaceNum = endpointToInterface(epnum);
	switch (interfaceNum) {
	case INTERFACE_MSC: {
		USBD_MSC_DataOut(pdev, epnum);
	}
	break;
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
		//NEN_TODO: What are we supposed to return on invalid interface?
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
		//NEN_TODO: What are we supposed to do if the interface is invalid?
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
		return 0;
		break;
	}
}

static uint8_t  *USBD_Multi_GetDeviceQualifierDesc (uint16_t *length)
{
	*length = sizeof (USBD_Multi_DeviceQualifierDesc);
	return USBD_Multi_DeviceQualifierDesc;
}
