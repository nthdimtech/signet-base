#include "usbd_multi.h"
#include "usbd_msc.h"
#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "signetdev_common_priv.h"
#include "usb_raw_hid.h"

#define LSB(X) ((X) & 0xff)
#define MSB(X) ((X) >> 8)
#define WTB(X) LSB(X), MSB(X)

USBD_HandleTypeDef *g_pdev = NULL;

int endpointToInterface(uint8_t epNum)
{
	return ((int)(epNum & ~(0x80))) - 1;
}

uint8_t interfaceToEndpointIn(int interfaceNum)
{
	return (uint8_t)((interfaceNum + 1) | 0x80);
}

uint8_t interfaceToEndpointOut(int interfaceNum)
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

//
// Warning: If you change this structure you must also change it in usbd_hid.c
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
// Warning: If you change this structure you must also change it in usbd_hid.c
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

//
// Warning: If you change this structure you must also change it in usbd_hid.c
//
static const u8 keyboard_hid_report_descriptor[] = {
	0x05, 1, //Usage page (Generic desktop)
	0x09, 6, //Usage (Keyboard)
	0xA1, 1, //Collection (application)

	0x05, 7,    //Usage page (Key codes)
	0x19, 224,  //Usage min
	0x29, 231,  //Usage max
	0x15, 0,    //Logical min
	0x25, 1,    //Logical max
	0x75, 1,    //Report size
	0x95, 8,    //Report count
	0x81, 2,    //Input (Data, Variable, Absolute)

	0x95, 1,    //Report count
	0x75, 8,    //Report size
	0x15, 0,    //Logical minimum
	0x25, 0x65, //Logical maximum
	0x05, 0x7,  //Usage page
	0x19, 0,     //Usage min
	0x29, 0x65,  //Usage max
	0x81, 0,    //Input

	0xc0        //End collection
};

/* USB HID device HS Configuration Descriptor */
static uint8_t USBD_Multi_CfgHSDesc[] __attribute__((aligned (4))) = {
	0x09, /* bLength: Configuration Descriptor size */
	USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType: Configuration */
	USB_HID_CONFIG_DESC_SIZ, /* wTotalLength: Bytes returned */
	0x00,
	INTERFACE_MAX,         /*bNumInterfaces */
	0x01,         /*bConfigurationValue: Configuration value*/
	0x00,         /*iConfiguration: Index of string descriptor describing the configuration*/
	0x80,         /*bmAttributes: bus powered and Support Remote Wake-up */
	50,         /*MaxPower 100 mA: this current is used for detecting Vbus*/

	//
	// Keyboard descriptors
	//
	// Interface descriptor, Class descriptor, IN endpoint
	//

		/************** Keyboard descriptors  ****************/
		0x09,         /*bLength: Interface Descriptor size*/
		USB_DESC_TYPE_INTERFACE,/*bDescriptorType: Interface descriptor type*/
		INTERFACE_KEYBOARD,         /*bInterfaceNumber: Number of Interface*/
		0x00,         /*bAlternateSetting: Alternate setting*/
		0x02,         /*bNumEndpoints*/
		0x03,         /*bInterfaceClass: HID*/
		0x01,         /*bInterfaceSubClass : 1=BOOT, 0=no boot*/
		0x01,         /*nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse*/
		0,            /*iInterface: Index of string descriptor*/

		0x09,         /*bLength: HID Descriptor size*/
		HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
		0x11,         /*bcdHID: HID Class Spec release number*/
		0x01,
		0x00,         /*bCountryCode: Hardware target country*/
		0x01,         /*bNumDescriptors: Number of HID class descriptors to follow*/
		0x22,         /*bDescriptorType*/
		sizeof(keyboard_hid_report_descriptor),/*wItemLength: Total length of Report descriptor*/
		0x0,

		7,
		USB_DESC_TYPE_ENDPOINT,
		HID_KEYBOARD_EPIN_ADDR,
		3,
		HID_KEYBOARD_EPIN_SIZE, 0,
		7, //polling period

		7,
		USB_DESC_TYPE_ENDPOINT,
		HID_KEYBOARD_EPOUT_ADDR,
		3,
		HID_KEYBOARD_EPOUT_SIZE, 0,
		7, //polling period

	//
	// Command HID descriptors
	//
	// Interface descriptor, Class descriptor, IN endpoint, OUT endpoint
	//

		/************** Descriptor of Command HID interface  ****************/
		0x09,         /*bLength: Interface Descriptor size*/
		USB_DESC_TYPE_INTERFACE,/*bDescriptorType: Interface descriptor type*/
		INTERFACE_CMD,         /*bInterfaceNumber: Number of Interface*/
		0x00,         /*bAlternateSetting: Alternate setting*/
		0x02,         /*bNumEndpoints*/
		0x03,         /*bInterfaceClass: HID*/
		0x01,         /*bInterfaceSubClass : 1=BOOT, 0=no boot*/
		0x00,         /*nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse*/
		0,            /*iInterface: Index of string descriptor*/

		/******************** Descriptor of Command HID OUT ********************/
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
		LOBYTE(HID_CMD_EPIN_SIZE), HIBYTE(HID_CMD_EPIN_SIZE),
		1,          /*bInterval: Polling Interval */
		/******************** Descriptor of Command HID OUT endpoint ********************/
		0x07,          /*bLength: Endpoint Descriptor size*/
		USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
		HID_CMD_EPOUT_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
		0x03,          /*bmAttributes: Interrupt endpoint*/
		LOBYTE(HID_CMD_EPOUT_SIZE), HIBYTE(HID_CMD_EPOUT_SIZE),
		1,          /*bInterval: Polling Interval */

	//
	// FIDO descriptors
	//
	// Interface descriptor, Class descriptor, IN endpoint, OUT endpoint
	//

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

		/******************** Class descriptor of FIDO HID ********************/
		0x09,         /*bLength: HID Descriptor size*/
		HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
		0x11, 0x01,          /*bcdHID: HID Class Spec release number*/
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
		HID_FIDO_EPIN_SIZE, 0x00,
		10,          /*bInterval: Polling Interval */

		/******************** Descriptor of FIDO HID OUT endpoint ********************/
		0x07,          /*bLength: Endpoint Descriptor size*/
		USB_DESC_TYPE_ENDPOINT, /*bDescriptorType:*/
		HID_FIDO_EPOUT_ADDR,     /*bEndpointAddress: Endpoint Address (IN)*/
		0x03,          /*bmAttributes: Interrupt endpoint*/
		HID_FIDO_EPOUT_SIZE, 0x00,
		HID_HS_BINTERVAL,          /*bInterval: Polling Interval */

	//
	// Mass storage descriptors
	//
	// Interface descriptor, IN endpoint, OUT endpoint
	//
		/********************  Mass Storage interface ********************/
		0x09,   /* bLength: Interface Descriptor size */
		USB_DESC_TYPE_INTERFACE,   /* bDescriptorType: */
		INTERFACE_MSC,   /* bInterfaceNumber: Number of Interface */
		0x00,   /* bAlternateSetting: Alternate setting */
		0x02,   /* bNumEndpoints*/
		0x08,   /* bInterfaceClass: MSC Class */
		0x06,   /* bInterfaceSubClass : SCSI transparent*/
		0x50,   /* nInterfaceProtocol */
		0x0,          /* iInterface: */

		/********************  Mass Storage Endpoints ********************/
		0x07,   /*Endpoint descriptor length = 7*/
		USB_DESC_TYPE_ENDPOINT,   /*Endpoint descriptor type */
		MSC_EPIN_ADDR,   /*Endpoint address (IN, address 1) */
		0x02,   /*Bulk endpoint type */
		LOBYTE(MSC_EPIN_SIZE), HIBYTE(MSC_EPIN_SIZE),
		0x00,   /*Polling interval in milliseconds */

		0x07,   /*Endpoint descriptor length = 7 */
		USB_DESC_TYPE_ENDPOINT,   /*Endpoint descriptor type */
		MSC_EPOUT_ADDR,   /*Endpoint address (OUT, address 1) */
		0x02,   /*Bulk endpoint type */
		LOBYTE(MSC_EPOUT_SIZE), HIBYTE(MSC_EPOUT_SIZE),
		0x00,    /*Polling interval in milliseconds*/
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

USBD_HID_HandleTypeDef s_cmdHIDClassData __attribute__((aligned(16)));
static USBD_HID_HandleTypeDef s_fidoHIDClassData __attribute__((aligned(16)));
static USBD_HID_HandleTypeDef s_keyboardHIDClassData __attribute__((aligned(16)));
static USBD_MSC_BOT_HandleTypeDef s_SCSIMSCClassData __attribute__((aligned(16)));

static uint8_t  USBD_Multi_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	/* Open EP IN */
	g_pdev = pdev;
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
	s_cmdHIDClassData.state = HID_IDLE;
	s_cmdHIDClassData.packetSize = HID_CMD_EPOUT_SIZE;
	USBD_LL_PrepareReceive (pdev, HID_CMD_EPOUT_ADDR, s_cmdHIDClassData.rx_buffer, s_cmdHIDClassData.packetSize);

	pdev->pClassData[INTERFACE_FIDO] = &s_fidoHIDClassData;
	USBD_LL_OpenEP(pdev, HID_FIDO_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPIN_SIZE);
	pdev->ep_in[HID_FIDO_EPIN_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, HID_FIDO_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_FIDO_EPOUT_SIZE);
	pdev->ep_in[HID_FIDO_EPOUT_ADDR & 0xFU].is_used = 1U;
	s_fidoHIDClassData.packetSize = HID_FIDO_EPOUT_SIZE;
	s_fidoHIDClassData.state = HID_IDLE;
	USBD_LL_PrepareReceive (pdev, HID_FIDO_EPOUT_ADDR, s_fidoHIDClassData.rx_buffer, s_fidoHIDClassData.packetSize);

	pdev->pClassData[INTERFACE_KEYBOARD] = &s_keyboardHIDClassData;
	USBD_LL_OpenEP(pdev, HID_KEYBOARD_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_KEYBOARD_EPIN_SIZE);
	pdev->ep_in[HID_KEYBOARD_EPIN_ADDR & 0xFU].is_used = 1U;
	USBD_LL_OpenEP(pdev, HID_KEYBOARD_EPOUT_ADDR, USBD_EP_TYPE_INTR, HID_KEYBOARD_EPOUT_SIZE);
	pdev->ep_in[HID_KEYBOARD_EPOUT_ADDR & 0xFU].is_used = 1U;
	s_keyboardHIDClassData.packetSize = HID_KEYBOARD_EPOUT_SIZE;
	s_keyboardHIDClassData.state = HID_IDLE;
	USBD_LL_PrepareReceive (pdev, HID_KEYBOARD_EPOUT_ADDR, s_keyboardHIDClassData.rx_buffer, s_keyboardHIDClassData.packetSize);
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

	/* Close Keyboard HID EPs */
	USBD_LL_CloseEP(pdev, HID_FIDO_EPIN_ADDR);
	pdev->ep_in[HID_FIDO_EPIN_ADDR & 0xFU].is_used = 0U;

	for (int i = 0; i < 5; i++) {
		if(pdev->pClassData[i] != NULL) {
			pdev->pClassData[i] = NULL;
		}
	}

	return USBD_OK;
}

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
		case INTERFACE_KEYBOARD:
			return USBD_HID_Setup(pdev, req);
			break;
		default:
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

static uint8_t  *USBD_Multi_GetFSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_Multi_CfgHSDesc);
	return USBD_Multi_CfgHSDesc;
}

static uint8_t  *USBD_Multi_GetHSCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_Multi_CfgHSDesc);
	return USBD_Multi_CfgHSDesc;
}

static uint8_t  *USBD_Multi_GetOtherSpeedCfgDesc (uint16_t *length)
{
	*length = sizeof (USBD_Multi_CfgHSDesc);
	return USBD_Multi_CfgHSDesc;
}

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
	case INTERFACE_KEYBOARD:
	case INTERFACE_FIDO: {
		USBD_HID_DataIn(pdev, epnum);
	} break;
	default:
		break;
	}
	return USBD_OK;
}

static uint8_t  USBD_Multi_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	g_pdev = pdev;
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
		USBD_HID_DataOut(pdev, epnum);
		break;
	default:
		break;
	}
	return USBD_OK;
}

static uint8_t  *USBD_Multi_GetDeviceQualifierDesc (uint16_t *length)
{
	*length = sizeof (USBD_Multi_DeviceQualifierDesc);
	return USBD_Multi_DeviceQualifierDesc;
}
