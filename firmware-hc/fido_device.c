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
#include "main.h"
#include "commands.h"
#include "memory_layout.h"
bool _up_disabled = false;

int device_is_nfc()
{
	return 0;
}

void authenticator_initialize()
{
}

void authenticator_sync_states()
{
	sync_root_block();
}

void authenticator_write_state(AuthenticatorState *state, int backup)
{
	if (backup) {
		g_root_page.fido2_auth_state_backup = *state;
	} else {
		g_root_page.fido2_auth_state = *state;
	}
}

void authenticator_read_state(AuthenticatorState * state)
{
	*state = g_root_page.fido2_auth_state;
}

void authenticator_read_backup_state(AuthenticatorState * state)
{
	*state = g_root_page.fido2_auth_state_backup;
}

int authenticator_is_backup_initialized()
{
	return g_root_page.fido2_auth_state_backup.is_initialized;
}

uint32_t ctap_atomic_count(int sel)
{
	//HC_TODO: what is sel?
	static uint32_t count = 1;
	return count++;
}

uint32_t __device_status = 0;

void device_set_status(uint32_t status)
{
    if (status != CTAPHID_STATUS_IDLE && __device_status != status)
    {
        ctaphid_update_status(status);
    }
    __device_status = status;
}

uint32_t millis()
{
	return HAL_GetTick();
}

void ctap_reset_rk()
{
	memset(&g_root_page.rk_store, 0xff, sizeof(g_root_page.rk_store));
}

uint32_t ctap_rk_size()
{
	return RK_NUM;
}

void ctap_store_rk(int index, CTAP_residentKey * rk)
{
	if (index < RK_NUM) {
		memmove(g_root_page.rk_store.rks + index, rk, sizeof(CTAP_residentKey));
		sync_root_block();
	} else {
		assert(0);
	}
}

void ctap_load_rk(int index, CTAP_residentKey * rk)
{
	if (index < RK_NUM) {
		memmove(rk, g_root_page.rk_store.rks + index, sizeof(CTAP_residentKey));
	} else {
		assert(0);
	}
}

void ctap_overwrite_rk(int index, CTAP_residentKey * rk)
{
	if (index < RK_NUM) {
		memmove(g_root_page.rk_store.rks + index, rk, sizeof(*rk));
		sync_root_block();
	} else {
		assert(0);
	}
}

void device_disable_up(bool request_active)
{
	_up_disabled = request_active;
}

void device_wink()
{
	//HC_TODO: What are we supposed to do here?
}

static u8 ctaphid_tx_buffer[2048] __attribute__((aligned(4)));
static volatile int ctap_hid_bytes_read = 0;
static volatile int ctap_hid_bytes_write = 0;

int usb_tx_pending(int x);

void ctaphid_write_block(uint8_t * data)
{
	__disable_irq();
	memcpy(ctaphid_tx_buffer + ctap_hid_bytes_read, data, HID_MESSAGE_SIZE);
	if (ctap_hid_bytes_read == ctap_hid_bytes_write) {
		usb_send_bytes(HID_FIDO_EPIN_ADDR, ctaphid_tx_buffer + ctap_hid_bytes_write, HID_MESSAGE_SIZE);
	}
	ctap_hid_bytes_read += HID_MESSAGE_SIZE;
	ctap_hid_bytes_read = ctap_hid_bytes_read % 2048;
	if (ctap_hid_bytes_read == ctap_hid_bytes_write) {
		assert(0);
	}
	__enable_irq();
}

void ctaphid_write_packet_sent()
{
	__disable_irq();
	if (ctap_hid_bytes_read != ctap_hid_bytes_write) {
		ctap_hid_bytes_write += HID_MESSAGE_SIZE;
		ctap_hid_bytes_write = ctap_hid_bytes_write % 2048;
		if (ctap_hid_bytes_read != ctap_hid_bytes_write) {
			usb_send_bytes(HID_FIDO_EPIN_ADDR, ctaphid_tx_buffer + ctap_hid_bytes_write, HID_MESSAGE_SIZE);
		}
	}
	__enable_irq();
}
