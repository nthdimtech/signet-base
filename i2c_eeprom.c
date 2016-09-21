#include "types.h"
#include "regmap.h"
#include "print.h"
#include "i2c_eeprom.h"

#define EEPROM_P I2C1
#define I2C_HANDLER i2c1_handler

extern int ms_count;
u8 eeprom_buf[256];
static u8 *eeprom_rx_buf;
static u8 *eeprom_tx_buf;
static volatile u8 eeprom_len = 0;
static volatile u8 eeprom_pos = 0;
static int read_req = 0;
static int eeprom_rx_len = 0;

static int tx_pending = 0;
static u32 last_tx_ms = 0;

#define I2C_CR2_REQ_MASK (I2C_CR2_AUTOEND | I2C_CR2_START | I2C_CR2_STOP | \
		I2C_CR2_RD_WRN | I2C_CR2_NBYTES_MASK)

void i2c_rx_complete()
{
}

void i2c_tx_complete()
{
}

#define PREFIX_HANDLER(X) X##_handler

void I2C_HANDLER()
{
	if (eeprom_len) {
		if (EEPROM_P->isr & I2C_ISR_TXIS) {
			EEPROM_P->txdr = eeprom_tx_buf[eeprom_pos];
			eeprom_pos++;
			if (eeprom_pos == eeprom_len) {
				if (read_req) {
					read_req = 0;
					eeprom_pos = 0;
					eeprom_len = eeprom_rx_len;
					EEPROM_P->cr2 = (EEPROM_P->cr2 & ~I2C_CR2_REQ_MASK) |
						(eeprom_len * I2C_CR2_NBYTES_FACTOR) |
						I2C_CR2_AUTOEND |
						I2C_CR2_RD_WRN;
					EEPROM_P->cr2 |= I2C_CR2_START;
				} else {
					last_tx_ms = ms_count;
					tx_pending = 1;
				}
			}
		}
		if (EEPROM_P->isr & I2C_ISR_RXNE) {
			eeprom_rx_buf[eeprom_pos] = EEPROM_P->rxdr;
			eeprom_pos++;
			if (eeprom_pos == eeprom_len) {
				i2c_rx_complete();
			}
		}
	}
}

void i2c_eeprom_init(u8 slave_addr)
{
	//EEPROM_P->timingr = 0xf032ffff; //Super slow mode
	//p->timingr = 0x30420f13; //100khz mode
	EEPROM_P->timingr = 0x10320309; //400khz mode
	EEPROM_P->cr1 |= I2C_CR1_ANF_OFF | I2C_CR1_DNF_FACTOR; //Noise filtering
	EEPROM_P->cr1 |= I2C_CR1_PE; //Enable port
	EEPROM_P->cr1 |= I2C_CR1_TXIE | I2C_CR1_RXIE; //Enable interrupts
	EEPROM_P->cr2 = (EEPROM_P->cr2 &  ~I2C_CR2_SADDR_MASK) | slave_addr;
}

void i2c_eeprom_idle()
{
	if (tx_pending && ms_count >= (last_tx_ms + 6)) {
		tx_pending = 0;
		i2c_tx_complete();
	}
}

int i2c_eeprom_write_page_direct(u8 *data, u8 data_len)
{
	eeprom_len = data_len;
	eeprom_pos = 0;
	eeprom_tx_buf = data;
	EEPROM_P->cr2 = (EEPROM_P->cr2 & ~I2C_CR2_REQ_MASK) |
		(eeprom_len * I2C_CR2_NBYTES_FACTOR) |
		I2C_CR2_AUTOEND;
	EEPROM_P->cr2 |= I2C_CR2_START;
	return 0;
}

int i2c_eeprom_write_page(u16 addr, u8 *data, u8 data_len)
{
	eeprom_buf[0] = addr >> 8;
	eeprom_buf[1] = addr & 0xff;
	for (int i = 0; i < data_len; i++) {
		eeprom_buf[i + 2] = data[i];
	}
	return i2c_eeprom_write_page_direct(eeprom_buf, data_len + 2);
}

int i2c_eeprom_read_page(u16 addr, u8 *data, u8 data_len)
{
	eeprom_buf[0] = addr >> 8;
	eeprom_buf[1] = addr & 0xff;
	eeprom_pos = 0;
	eeprom_len = 2;
	eeprom_rx_buf = data;
	eeprom_tx_buf = eeprom_buf;

	read_req = 1;
	eeprom_rx_len = data_len;

	EEPROM_P->cr2 = (EEPROM_P->cr2 & ~I2C_CR2_REQ_MASK) |
		(eeprom_len * I2C_CR2_NBYTES_FACTOR) |
		I2C_CR2_AUTOEND;
	EEPROM_P->cr2 |= I2C_CR2_START;
	return 0;
}
