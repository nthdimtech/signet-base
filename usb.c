#include "usb.h"
#include "print.h"
#include "mem.h"
#include "usb_serial.h"
#include "usb_raw_hid.h"
#include "usb_keyboard.h"
#include "hid_keyboard.h"
#include "usb_storage.h"
#include "config.h"
#include "usb_driver.h"

int usb_device_addr = 0;

#define LSB(X) ((X) & 0xff)
#define MSB(X) ((X) >> 8)

#define WTB(X) LSB(X), MSB(X)

#define USB_VER_BCD 0x101

#define ARRAY_COUNT(ary) (sizeof(ary)/sizeof(typeof(ary[0])))

#define USB_BULK_ATTR 0x2
#define USB_INTR_ATTR 0x3
#define USB_ENDPOINT_DESC 0x5
#define USB_INTERFACE_DESC 0x4

#define USB_HID_CLASS 0x3
#define USB_HID_BOOT_SUBCLASS 0x1
#define USB_HID_KEYBOARD_PROTOCOL 0x1

#define USB_ENDPOINT_TX (0x80)

#define USB_STANDARD_REQ 0
#define USB_CLASS_REQ 1

#define USB_CLASS_REQ_CLEAR_FEATURE 1

#define USB_CDC_REQ_SET_LINE_CODING 32
#define USB_CDC_REQ_GET_LINE_CODING 33
#define USB_CDC_REQ_SET_LINE_STATE 34

extern int ms_count;

const u8 keyboard_interface[] = {
	9,
	USB_INTERFACE_DESC,
	KEYBOARD_INTERFACE,
	0,
	1, //# endpoints
	USB_HID_CLASS,
	USB_HID_BOOT_SUBCLASS,
	USB_HID_KEYBOARD_PROTOCOL,
	3  //Interface string
};

const u8 keyboard_hid_report_descriptor[] = {
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

const u8 keyboard_hid_descriptor[] = {
	9 ,
	0x21, //HID descriptor
	WTB(0x101), //BCD HID
	0, //No target country
	1, //num HID decriptors
	0x22, //Report decriptor
	sizeof(keyboard_hid_report_descriptor), //report decriptor size
	0
};

const u8 keyboard_endpoint[] = {
	7,
	USB_ENDPOINT_DESC,
	KEYBOARD_ENDPOINT | USB_ENDPOINT_TX,
	USB_INTR_ATTR,
	WTB(KEYBOARD_PACKET_SIZE),
	10 //10ms polling period
};

const u8 raw_hid_interface[] = {
	9,
	USB_INTERFACE_DESC,
	RAW_HID_INTERFACE,
	0,
	2, //# endpoints
	USB_HID_CLASS,
	0x0, //No subclass
	0x0, //No protocol
	0, //No string
};

const u8 raw_hid_report_descriptor[] = {
	0x06, LSB(USB_RAW_HID_USAGE_PAGE), MSB(USB_RAW_HID_USAGE_PAGE),
	0x0A, LSB(USB_RAW_HID_USAGE), MSB(USB_RAW_HID_USAGE),
	0xA1, 0x01,				// Collection 0x01
	0x75, 0x08,				// report size = 8 bits
	0x15, 0x00,				// logical minimum = 0
	0x26, 0xFF, 0x00,			// logical maximum = 255
	0x95, RAW_HID_TX_SIZE,			// report count
	0x09, 0x01,				// usage
	0x81, 0x02,				// Input (array)
	0x95, RAW_HID_RX_SIZE,			// report count
	0x09, 0x02,				// usage
	0x91, 0x02,				// Output (array)
	0xC0					// end collection
};

const u8 raw_hid_descriptor[] = {
	9,					// bLength
	0x21,					// bDescriptorType
	0x11, 0x01,				// bcdHID
	0,					// bCountryCode
	1,					// bNumDescriptors
	0x22,					// bDescriptorType
	WTB(sizeof(raw_hid_report_descriptor)),	// wDescriptorLength
};

const u8 raw_hid_tx_endpoint[] = {
	7,
	USB_ENDPOINT_DESC,
	RAW_HID_TX_ENDPOINT | USB_ENDPOINT_TX,
	USB_INTR_ATTR,
 	WTB(RAW_HID_TX_SIZE),
	RAW_HID_TX_INTERVAL,
};

const u8 raw_hid_rx_endpoint[] = {
	7,
	USB_ENDPOINT_DESC,					// bDescriptorType
	RAW_HID_RX_ENDPOINT,			// bEndpointAddress
	USB_INTR_ATTR,					// bmAttributes (0x03=intr)
	RAW_HID_RX_SIZE, 0,			// wMaxPacketSize
	RAW_HID_RX_INTERVAL			// bInterval
};

const u8 cdc_interface_association[] = {
	8,                                      // bLength
        11,                                     // bDescriptorType
        CDC_STATUS_INTERFACE,			// bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0                                      // iFunction
};

const u8 cdc_status_interface[] = {
	9,                                      // bLength
        4,                                      // bDescriptorType
        CDC_STATUS_INTERFACE,			// bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0                                      // iInterface
};

const u8 cdc_header_functional_descriptor[] = {
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
	0x10, 0x01                              // bcdCDC
};

const u8 cdc_call_management_functional_descriptor[] = {
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        CDC_DATA_INTERFACE			// bDataInterface
};

const u8 cdc_acm_control_management_functional_descriptor[] = {
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06                                   // bmCapabilities
};

const u8 cdc_union_functional_descriptor[] = {
	5,                                      // bFunctionLength
	0x24,                                   // bDescriptorType
	0x06,                                   // bDescriptorSubtype
	CDC_STATUS_INTERFACE,                   // bMasterInterface
	CDC_DATA_INTERFACE                     // bSlaveInterface0
};

const u8 cdc_acm_endpoint_descriptor[] = {
        7,                                      // bLength
        5,                                      // bDescriptorType
        CDC_ACM_ENDPOINT | 0x80,                // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        WTB(CDC_ACM_PACKET_SIZE),                        // wMaxPacketSize
        64                                     // bInterval
};

const u8 cdc_acm_data_interface[] = {
        9,                                      // bLength
        4,                                      // bDescriptorType
        CDC_DATA_INTERFACE,                     // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0                                      // iInterface
};

const u8 cdc_acm_rx_endpoint[] = {
        7,                                      // bLength
        5,                                      // bDescriptorType
        CDC_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        WTB(CDC_RX_SIZE),                         // wMaxPacketSize
        0                                  // bInterval
};

const u8 cdc_acm_tx_endpoint[] = {
        7,                                      // bLength
        5,                                      // bDescriptorType
        CDC_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        WTB(CDC_TX_SIZE),                         // wMaxPacketSize
        0                                      // bInterval
};

const u8 device_descriptor[] = {
	18,//18 byte descriptor
	1, //device descriptor
	WTB(USB_VER_BCD), //USB version
	0,//0xEF, //misc class
	0,//0x2, //No subclass
	0,//1, //Interface specific protocol
	CTRL_RX_SIZE, //> CTRL_TX_SIZE ? CTRL_RX_SIZE : CTRL_TX_SIZE, //max packet size
	WTB(USB_VENDOR_ID), //Vendor ID
	WTB(USB_SIGNET_COMMON_PRODUCT_ID), //Product ID
	WTB(USB_REV_ID), //Rev ID
	1, //Manufacturer string
	2, //Product string
	0, //serial number
	1, //one configuration
};

const u8 device_qualifier[] = {
	10,//10 byte descriptor
	6, //device qualifier
	WTB(USB_VER_BCD), //USB version
	0xff, 0, //vendor specific class + no subclass
	0xff, //vendor specific protocol
	64,  //max packet size of 64
	1, //Only one configuration
	0 //Reserved
};

const u16 string_zero[] = {
	4 | (3 << 8),
	LANGID_US_ENGLISH
};

const u16 string_product[] = {
	14 | (3 << 8),
	u'S', //1
	u'i', //2
	u'g', //3
	u'n', //4
	u'e', //5
	u't', //6
};

const u16 string_manufacturer[] = {
	28 | (3 << 8),
	u'N', //1
	u't', //2
	u'h', //3
	u' ', //4
	u'D', //5
	u'i', //6
	u'm', //7
	u'e', //8
	u'n', //9
	u's', //10
	u'i', //11
	u'o', //12
	u'n'  //13
};

const u16 string_keyboard_if[] = {
	30 | (3 << 8),
	u'M', //1
	u'a', //2
	u'g', //3
	u'i', //4
	u'c', //5
	u' ', //6
	u'k', //7
	u'e', //8
	u'y', //9
	u'b', //10
	u'o', //11
	u'a', //12
	u'r', //13
	u'd'  //14
};

const u16 *strings[] = {
	string_zero,
	string_manufacturer,
	string_product,
	string_keyboard_if,
};

void usb_set_device_address(int addr)
{
	usb_device_addr = addr;
	dprint_s("USB: SET ADDR ");
	dprint_dec(addr);
	dprint_s("\r\n");
}

extern const u8 device_config_desktop[];

const u8 *device_configuration_desktop[] = {
	device_config_desktop, //9

	cdc_interface_association,
		cdc_status_interface,
			cdc_header_functional_descriptor,
			cdc_call_management_functional_descriptor,
			cdc_acm_control_management_functional_descriptor,
			cdc_union_functional_descriptor,
			cdc_acm_endpoint_descriptor,
		cdc_acm_data_interface,
			cdc_acm_rx_endpoint,
	cdc_acm_tx_endpoint,

	keyboard_interface, //9
		keyboard_hid_descriptor, //9
		keyboard_endpoint, //7

	raw_hid_interface, //9
		raw_hid_descriptor, //9
		raw_hid_tx_endpoint, //7
		raw_hid_rx_endpoint,//7
};

extern const u8 device_config_common[];

const u8 *device_configuration_common[] = {
	device_config_common, //9

	cdc_interface_association,
		cdc_status_interface,
			cdc_header_functional_descriptor,
			cdc_call_management_functional_descriptor,
			cdc_acm_control_management_functional_descriptor,
			cdc_union_functional_descriptor,
			cdc_acm_endpoint_descriptor,
		cdc_acm_data_interface,
			cdc_acm_rx_endpoint,
	cdc_acm_tx_endpoint,

	raw_hid_interface, //9
		raw_hid_descriptor, //9
		raw_hid_tx_endpoint, //7
		raw_hid_rx_endpoint,//7
};

#define USB_CONFIG_TOTAL_SIZE_DESKTOP ( \
	9 /* sizeof(device_config) */ + \
	sizeof(cdc_interface_association) + \
		sizeof(cdc_status_interface) + \
			sizeof(cdc_header_functional_descriptor) + \
			sizeof(cdc_call_management_functional_descriptor) + \
			sizeof(cdc_acm_control_management_functional_descriptor) + \
			sizeof(cdc_union_functional_descriptor) + \
			sizeof(cdc_acm_endpoint_descriptor) + \
		sizeof(cdc_acm_data_interface) + \
			sizeof(cdc_acm_rx_endpoint) + \
	sizeof(cdc_acm_tx_endpoint) + \
	sizeof(keyboard_interface) + \
		sizeof(keyboard_hid_descriptor) + \
		sizeof(keyboard_endpoint) + \
	sizeof(raw_hid_interface) + \
		sizeof(raw_hid_descriptor) + \
		sizeof(raw_hid_tx_endpoint) + \
		sizeof(raw_hid_rx_endpoint) \
		)

#define USB_CONFIG_TOTAL_SIZE_COMMON ( \
	9 /* sizeof(device_config) */ + \
	sizeof(cdc_interface_association) + \
		sizeof(cdc_status_interface) + \
			sizeof(cdc_header_functional_descriptor) + \
			sizeof(cdc_call_management_functional_descriptor) + \
			sizeof(cdc_acm_control_management_functional_descriptor) + \
			sizeof(cdc_union_functional_descriptor) + \
			sizeof(cdc_acm_endpoint_descriptor) + \
		sizeof(cdc_acm_data_interface) + \
			sizeof(cdc_acm_rx_endpoint) + \
	sizeof(cdc_acm_tx_endpoint) + \
	sizeof(raw_hid_interface) + \
		sizeof(raw_hid_descriptor) + \
		sizeof(raw_hid_tx_endpoint) + \
		sizeof(raw_hid_rx_endpoint) \
		)

const u8 device_config_desktop[] = {
	9,
	2, //device type
	WTB(USB_CONFIG_TOTAL_SIZE_DESKTOP), //total length
	NUM_INTERFACES, //number of interfaces
	1, //1 configuration
	0, //No configuration name string
	0x80, //USB powered no remote wakeup
	100 //200ma power limit + Interface descriptor length
};

const u8 device_config_common[] = {
	9,
	2, //device type
	WTB(USB_CONFIG_TOTAL_SIZE_COMMON), //total length
	NUM_INTERFACES, //number of interfaces
	1, //1 configuration
	0, //No configuration name string
	0x80, //USB powered no remote wakeup
	100 //200ma power limit + Interface descriptor length
};

static int restart_byte = -1;
static int transaction_length = -1;

void usb_get_device_descriptor(int type, int index, int lang_id, int length)
{
	int current_max;
	const void *data = NULL;
	int data_length;
	switch (type) {
	case 1: //Device
		dprint_s("USB: GET DEVICE DESCRIPTOR ");
		dprint_dec(length);
		dprint_s("\r\n");
		data = device_descriptor;
		data_length = device_descriptor[0];
		break;
	case 2: //Configuration
		dprint_s("USB: GET DEVICE CONFIGURATION (");
		dprint_dec(length);
		dprint_s(")\r\n");
		current_max = length > CTRL_TX_SIZE ? CTRL_TX_SIZE : length;
		restart_byte = usb_send_descriptors(device_configuration_desktop, 0, ARRAY_COUNT(device_configuration_desktop), current_max);
		transaction_length = length;
		break;
	case 3: //String
		dprint_s("USB: GET STRING ");
		dprint_dec(index);
		dprint_s(" ");
		dprint_dec(lang_id);
		dprint_s("\r\n");
		data = strings[index];
		data_length = (strings[index][0] & 0xff) * 2;
		break;
	case 4: //Interface
		dprint_s("USB: GET INTERFACE");
		dprint_dec(index);
		dprint_s(" ");
		dprint_dec(lang_id);
		dprint_s("\r\n");
		data = keyboard_interface;
		data_length = keyboard_interface[0];
		break;
	case 5: //Endpoint
		dprint_s("USB: GET ENDPOINT");
		dprint_dec(index);
		dprint_s(" ");
		dprint_dec(lang_id);
		dprint_s("\r\n");
		data = keyboard_endpoint;
		data_length = keyboard_endpoint[0];
		break;
	case 6: //Device qualifier
		dprint_s("USB: GET DEVICE QUALIFIER\r\n");
		data = device_qualifier;
		data_length = device_qualifier[0];
		break;
	default:
		dprint_s("USB: get device descriptor: type = ");
		dprint_dec(type);
		dprint_s("\r\n");
		break;
	}
	if (data) {
		int send_length = length > data_length ? data_length : length;
		usb_send_bytes(CONTROL_ENDPOINT, data, send_length);
	}
}

void usb_get_interface_descriptor(int type, int index, int interface, int length)
{
	int matched = 1;
	switch (interface) {
	case KEYBOARD_INTERFACE:
		switch (type) {
		case 0x22: //report descriptor
			usb_send_bytes(CONTROL_ENDPOINT, keyboard_hid_report_descriptor, sizeof(keyboard_hid_report_descriptor));
			dprint_s("USB: get keyboard report descriptor: index = ");
			dprint_dec(index);
			dprint_s(" ");
			dprint_dec(length);
			dprint_s("\r\n");
			break;
		default:
			matched = 0;
			break;
		}
		break;
	case RAW_HID_INTERFACE:
		switch (type) {
		case 0x22: //report descriptor
			usb_send_bytes(CONTROL_ENDPOINT, raw_hid_report_descriptor, sizeof(raw_hid_report_descriptor));
			dprint_s("USB: get rawhid report descriptor: index = ");
			dprint_dec(index);
			dprint_s(" ");
			dprint_dec(length);
			dprint_s("\r\n");
			break;
		default:
			matched = 0;
		}
		break;
	}
	if (!matched) {
		dprint_s("USB: get interface descriptor: type = ");
		dprint_hex(type);
		dprint_s(", if = ");
		dprint_dec(interface);
		dprint_s(", index = ");
		dprint_dec(index);
		dprint_s("\r\n");
	}
}

void usb_get_device_status()
{
	u16 val = 0;
	usb_send_words(CONTROL_ENDPOINT, &val, 2);
}

void usb_standard_device_req(int dir, int req, int w_value, int w_index, int w_length)
{
	switch (req) {
	case 6: //Get Descriptor
		usb_get_device_descriptor(w_value >> 8 , w_value & 0xff, w_index, w_length);
		break;
	case 5: //Set address
		usb_set_device_address(w_value);
		break;
	case 9: //Set configuration
		usb_set_device_configuration(w_value, w_length);
		break;
	case 0: //Get status
		usb_get_device_status();
		break;
	case 1: //Clear feature
	case 3: //Set feature
	case 7: //Set descriptor
	case 8: //Get configuration
	case 10: //Get interface
	case 11: //Set interface
	case 12: //Synch frame
	default:
		dprint_s("USB: standard device req ");
		dprint_dec(req);
		dprint_s("\r\n");
		break;
	}
}

void usb_standard_interface_req(int dir, int req, int w_value, int w_index, int w_length)
{
	switch (req) {
	case 6: //Get Descriptor
		usb_get_interface_descriptor(w_value >> 8 , w_value & 0xff, w_index, w_length);
		break;
	default:
		dprint_s("USB standard interface request: req = ");
		dprint_dec(req);
		dprint_s("\r\n");
		break;
	}
}

void usb_standard_endpoint_req(int dir, int req, int feature, int ep, int w_length)
{
	switch (req) {
	case 1: //CLEAR_FEATURE
		dprint_s("USB clear feature: ep = ");
		dprint_hex(ep);
		dprint_s(", feature = ");
		dprint_dec(feature);
		dprint_s("\r\n");
		break;
	case 3: //SET FEATURE
		dprint_s("USB set feature: ep = ");
		dprint_hex(ep);
		dprint_s(", feature = ");
		dprint_dec(feature);
		dprint_s("\r\n");
		break;
	default:
		dprint_s("USB standard endpoint request: id = ");
		dprint_dec(ep);
		dprint_s(",req = ");
		dprint_dec(req);
		dprint_s("\r\n");
		break;
	}
}


void usb_standard_req(int dir, int req, int dest, int w_value, int w_index, int w_length)
{
	switch(dest) {
	case 0: //device
		usb_standard_device_req(dir, req, w_value, w_index, w_length);
		break;
	case 1: //Interface
		usb_standard_interface_req(dir, req, w_value, w_index, w_length);
		break;
	case 2: //Endpoint
		usb_standard_endpoint_req(dir, req, w_value, w_index, w_length);
		break;
	default:
		dprint_s("USB standard request: dest = ");
		dprint_dec(dest);
		dprint_s("\r\n");
	}
}

int keyboard_idle_val = 0;
int raw_hid_idle_val = 0;
int set_line_coding_active = 0;
u8 raw_hid_report[RAW_HID_TX_SIZE];

void usb_class_req(int dir, int req, int dest, int w_value, int interface, int w_length)
{
	int matched = 1;
	switch (interface) {
	case KEYBOARD_INTERFACE:
		switch (req) {
		case HID_REQ_SET_IDLE: //SET_IDLE
			dprint_s("KEYBOARD HID: SET_IDLE ");
			dprint_dec(w_value);
			dprint_s("\r\n");
			keyboard_idle_val = w_value;
			break;
		default:
			matched = 0;
		}
		break;
	case CDC_STATUS_INTERFACE:
		switch (req) {
		case USB_CDC_REQ_SET_LINE_CODING: {
			set_line_coding_active = 1;
			dprint_s("USB CDC/ACM: SET_LINE_CODING_ACTIVE \r\n");
		} break;
		case USB_CDC_REQ_GET_LINE_CODING: {
			usb_send_bytes(CONTROL_ENDPOINT, (u8 *)&g_serial_line_coding, 7);
			dprint_s("USB CDC/ACM: GET_LINE_CODING\r\n");
		} break;
		case USB_CDC_REQ_SET_LINE_STATE:
			usb_serial_line_state((w_value & 2) >> 1, w_value & 1);
			break;
		default:
			matched = 0;
		}
		break;
	case RAW_HID_INTERFACE:
		switch (req) {
		case HID_REQ_GET_REPORT:
			usb_send_bytes(CONTROL_ENDPOINT, raw_hid_report, RAW_HID_TX_SIZE);
			dprint_s("RAW HID: GET_REPORT ");
			dprint_dec(w_length);
			dprint_s("\r\n");
			break;
		case HID_REQ_SET_IDLE:
			dprint_s("RAW HID: SET_IDLE ");
			dprint_dec(w_value);
			dprint_s("\r\n");
			raw_hid_idle_val = w_value;
			break;
		default:
			matched = 0;
			break;
		}
		break;
	default:
		matched = 0;
		break;
	}
	if (!matched) {
		dprint_s("USB class request: if = ");
		dprint_dec(interface);
		dprint_s(", req = ");
		dprint_dec(req);
		dprint_s("\r\n");
	}
}

void usb_setup(volatile usbw_t *packet, int count)
{
	usbw_t w1 = packet[0];
	usbw_t w2 = packet[1];
	usbw_t w3 = packet[2];
	usbw_t w4 = packet[3];

	int dest = w1 & 0x1f;
	int type = (w1 >> 5) & 0x3;
	int dir = (w1 >> 7) & 0x1;
	int req = (w1 >> 8) & 0xff;
	int w_value = w2;
	int w_index = w3;
	int w_length = w4;
	switch (type) {
	case USB_STANDARD_REQ: //standard
		usb_standard_req(dir, req, dest, w_value, w_index, w_length);
		break;
	case USB_CLASS_REQ: //class
		usb_class_req(dir, req, dest, w_value, w_index, w_length);
		break;
	default:
		dprint_s("USB setup request: type = ");
		dprint_hex(type);
		dprint_s("\r\n");
		break;
	}
	if (!w_length) {
		usb_send_bytes(CONTROL_ENDPOINT, 0, 0);
	}
}

static int usb_rx_ctrl(int setup, volatile usbw_t *data, int count)
{
	if (setup) {
		usb_setup(data, count);
	} else if (set_line_coding_active) {
		for (int i = 0; i < 7; i++) {
			((u8 *)(&g_serial_line_coding))[i] = (data[i>>1] >> (8*(i&1))) & 0xff;
		}
		u32 baud = ((u32)data[0]) | (((u32)data[1])<<16);
		dprint_s("USB CDC/ACM: SET_LINE_CODING ");
		dprint_dec(baud);
		dprint_s(" ");
		dprint_dec(g_serial_line_coding.stop_bits);
		dprint_s(" ");
		dprint_dec(g_serial_line_coding.parity);
		dprint_s(" ");
		dprint_dec(g_serial_line_coding.data_bits);
		dprint_s("\r\n");
		set_line_coding_active = 0;
		usb_send_bytes(CONTROL_ENDPOINT, 0, 0);
	} else {
		if (count != 0) {
			dprint_s("USB: Unexpected CTRL RX ");
			dprint_dec(count);
			dprint_s("\r\n");
		}
	}
	return !usb_tx_pending(CONTROL_ENDPOINT);
}

void usb_rx(int id, int setup, volatile usbw_t *data, int count)
{
	int valid_rx = 0;
	switch (id) {
	case CONTROL_ENDPOINT:
		valid_rx = usb_rx_ctrl(setup, data, count);
		break;
	case CDC_RX_ENDPOINT:
		valid_rx = usb_serial_rx(data, count);
		break;
	case RAW_HID_RX_ENDPOINT:
		valid_rx = usb_raw_hid_rx(data, count);
		break;
	default:
		dprint_s("USB RX: ");
		dprint_dec(id);
		dprint_s("\r\n");
	}
	if (valid_rx) {
		usb_valid_rx(id);
	}
}

void usb_tx(int id)
{
	switch (id) {
	case KEYBOARD_ENDPOINT:
		usb_tx_keyboard();
		break;
	case CONTROL_ENDPOINT:
		usb_tx_ctrl();
		if (!usb_tx_pending(CONTROL_ENDPOINT)) {
			usb_valid_rx(CONTROL_ENDPOINT);
		}
		break;
	case CDC_TX_ENDPOINT:
		usb_serial_tx();
		usb_valid_rx(CDC_RX_ENDPOINT);
		break;
	case RAW_HID_TX_ENDPOINT:
		usb_raw_hid_tx();
		break;
	default:
		dprint_s("Unexpected TX on endpoint ");
		dprint_dec(id);
		dprint_s("\r\n");
		usb_send_bytes(id, 0, 0);
		break;
	}
}


void usb_tx_ctrl()
{
	if (restart_byte >= 0) {
		int current_max = (transaction_length - restart_byte) > CTRL_TX_SIZE ? CTRL_TX_SIZE: (transaction_length - restart_byte);
		if (current_max) {
			dprint_s("USB: send descriptors ");
			dprint_dec(restart_byte);
			dprint_s(" ");
			dprint_dec(current_max);
			dprint_s("\r\n");
			restart_byte = usb_send_descriptors(device_configuration_desktop, restart_byte, ARRAY_COUNT(device_configuration_desktop), current_max);
		} else {
			restart_byte = -1;
		}
	}
}
