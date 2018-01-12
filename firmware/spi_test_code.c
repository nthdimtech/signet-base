
u8 cmd0[] = {
	0x40,
	0,
	0,
	0,
	0,
	0x95
};
void crc7(u8 *cmd)
{
	int i = 0;
	u16 crc;
	for (i = 0; i < 5; i++) {
		u16 x = cmd[i];
		crc += (x*x*x*x*x*x*x) + (x*x*x) + x;
	}
	cmd[i] = ((crc << 1) | 1) & 0xff;
}

void spi1_handler(void)
{
	static int dummy_frames = 16;
	static int cmd_index = 0;
	static int end_tx = 0;
	u32 sr = SPI1->sr;
	if (sr & SPI_SR_OVR) {
		dprint_s("SPI OVR error\r\n");
	}
	if (sr & SPI_SR_MODF) {
		dprint_s("SPI MODF error\r\n");
	}
	if (sr & SPI_SR_CRCERR) {
		dprint_s("SPI CRC error\r\n");
	}
	if (sr & SPI_SR_UDR) {
		dprint_s("SPI UDR error\r\n");
	}
	if (!end_tx) {
	if (!dummy_frames) {
		if (sr & SPI_SR_TXE) {
			if (cmd_index < 6) {
				*((volatile u8 *)&SPI1->dr) = cmd0[cmd_index];
				cmd_index++;
			} else if (cmd_index < (6 + 10)) {
				*((volatile u8 *)&SPI1->dr) = 0xff;
				cmd_index++;
			} else {
				//dprint_s("Command failed to get response\r\n");
				SPI1->cr2 &= ~SPI_CR2_TXEIE;
				cmd_index = 0;
				//dummy_frames = 16;
				//SDCARD_CS_PORT->BSRR = 1 << (SDCARD_CS_PIN);
				*((volatile u8 *)&SPI1->dr) = cmd0[cmd_index];
				cmd_index++;
			}
		}
	} else {
		if (sr & SPI_SR_TXE) {
			dummy_frames--;
			if (!dummy_frames) {
				dprint_s("SPI mode enter start\r\n");
				SDCARD_CS_PORT->BSRR = 1 << (SDCARD_CS_PIN+16);
				*((volatile u8 *)&SPI1->dr) = cmd0[cmd_index];
				cmd_index++;
			} else {
				*((volatile u8 *)&SPI1->dr) = 0xff;
			}
		}
	}
	}
	while (sr & SPI_SR_RXNE) {
		u8 v = SPI1->dr;
		if (v != 0xff) {
			dprint_s("Fini:");
			dprint_hex(v);
			dprint_s("\r\n");
			SPI1->cr2 &= ~SPI_CR2_TXEIE;
			end_tx = 1;
		}
		sr = SPI1->sr;
	}
}

void spi_startup()
{
	set_irq_priority(SPI1_IRQ, 128);
	enable_irq(SPI1_IRQ);
	RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
	//Enable SPI pins
	gpio_set_out(SDCARD_CS_PORT, SDCARD_CS_PIN);
	gpio_enable_af(SPI_CLK_PORT, SPI_CLK_PIN, SPI_CLK_AF);
	gpio_enable_af(SPI_MISO_PORT, SPI_MISO_PIN, SPI_MISO_AF);
	gpio_enable_af(SPI_MOSI_PORT, SPI_MOSI_PIN, SPI_MOSI_AF);

	delay(100);

	//crc7(cmd0);
	SPI1->cr2 = (SPI_CR2_DS_FACTOR * 7) |
		SPI_CR2_RXNEIE |
		SPI_CR2_TXEIE |
		SPI_CR2_ERRIE |
		SPI_CR2_FRXTH;
	SPI1->cr1 = SPI_CR1_SPE | //Enable
		(SPI_CR1_BR_FACTOR * 5) | //PCLK/256
		SPI_CR1_MSTR |
		SPI_CR1_SSM |
		SPI_CR1_SSI;
	SDCARD_CS_PORT->BSRR = (1 << SDCARD_CS_PIN);
}
