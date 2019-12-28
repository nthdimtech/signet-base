#ifndef __USBD_HID_H
#define __USBD_HID_H

#include  "usbd_ioreq.h"

#define HID_REQ_SET_PROTOCOL          0x0BU
#define HID_REQ_GET_PROTOCOL          0x03U

#define HID_REQ_SET_IDLE              0x0AU
#define HID_REQ_GET_IDLE              0x02U

#define HID_REQ_SET_REPORT            0x09U
#define HID_REQ_GET_REPORT            0x01U

#include "types.h"

uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
void USBD_HID_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);
void  USBD_HID_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);

uint8_t USBD_HID_SendReport     (USBD_HandleTypeDef  *pdev,
                                 int interfaceNum,
                                 const uint8_t *report,
                                 uint16_t len);

uint32_t USBD_HID_GetPollingInterval (USBD_HandleTypeDef *pdev);

void usb_send_bytes(int ep, const u8 *data, int length);

typedef enum {
	HID_IDLE = 0,
	HID_BUSY,
}
HID_StateTypeDef;

typedef struct {
	uint8_t rx_buffer[64];
	uint32_t             Protocol;
	uint32_t             IdleState;
	uint32_t             AltSetting;
	HID_StateTypeDef     state;
	const uint8_t *tx_report;
} USBD_HID_HandleTypeDef;
#endif
