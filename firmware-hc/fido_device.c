#include "ctap.h"
#include "u2f.h"
#include "ctaphid.h"
#include "ctap_parse.h"
#include "ctap_errors.h"
#include "cose_key.h"
#include "crypto.h"
#include "util.h"
#include "log.h"
#include "device.h"

#include "wallet.h"
#include "extensions.h"

#include "device.h"
#include "data_migration.h"
#include "stm32f7xx_hal.h"
#include "usbd_multi.h"
#include "usbd_hid.h"

int device_is_nfc()
{
	return 0;
}

void authenticator_write_state(AuthenticatorState *state, int backup)
{
	//HC_TODO
}

uint32_t ctap_atomic_count(int sel)
{
	//HC_TODO: what is sel?
	static uint32_t count = 1;
	return count++;
}

void device_set_status(uint32_t status)
{
	//HC_TODO
}

int ctap_user_presence_test(uint32_t delay)
{
	//HC_TODO
	return 1;
}

int ctap_generate_rng(uint8_t * dst, size_t num)
{
	//HC_TODO
	return 1;
}

uint32_t millis()
{
	return HAL_GetTick();
}

void ctap_reset_rk()
{

}

uint32_t ctap_rk_size()
{
	return 128; //HC_TODO
}

void ctap_store_rk(int index,CTAP_residentKey * rk)
{
	//HC_TODO
}

void ctap_load_rk(int index,CTAP_residentKey * rk)
{
	//HC_TODO
}

void ctap_overwrite_rk(int index, CTAP_residentKey * rk)
{
	//HC_TODO
}

void device_disable_up(bool request_active)
{
	//HC_TODO
}

void device_wink()
{
	//HC_TODO
}

void ctaphid_write_block(uint8_t * data)
{
	usb_send_bytes(HID_FIDO_EPIN_ADDR, data, 64); //HC_TODO
}
