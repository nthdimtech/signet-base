#ifndef __USBD_MSC_DATA_H
#define __USBD_MSC_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_conf.h"

#define MODE_SENSE6_LEN                    8U
#define MODE_SENSE10_LEN                   8U
#define LENGTH_INQUIRY_PAGE00              7U
#define LENGTH_FORMAT_CAPACITIES           20U

extern const uint8_t MSC_Page00_Inquiry_Data[];
extern const uint8_t MSC_Mode_Sense6_data[];
extern const uint8_t MSC_Mode_Sense10_data[] ;

#ifdef __cplusplus
}
#endif

#endif
