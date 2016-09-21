#ifndef I2C_H
#define I2C_H

#include "regmap.h"

void i2c_eeprom_init(u8 slave_addr);
int i2c_eeprom_write_page_direct(u8 *data, u8 data_len);
int i2c_eeprom_write_page(u16 addr, u8 *data, u8 data_len);
int i2c_eeprom_read_page(u16 addr, u8 *data, u8 data_len);
void i2c_eeprom_idle();
int i2c_eeprom_write_pending();

#define NUM_EEPROM_BLOCKS 512

#endif
