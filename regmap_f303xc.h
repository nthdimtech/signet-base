#ifndef REGMAP_F303XC_H
#define REGMAP_F303XC_H

#define MCU_F_SERIES 1

#define RCC_BASE_ADDR (0x40021000)
#define USB_BASE_ADDR (0x40005C00)
#define USB_PMA_ADDR (0x40006000)
#define EXTI_BASE_ADDR (0x40010400)
#define RTC_BASE_ADDR (0x40002800)
#define PWR_BASE_ADDR (0x40007000)
#define FLASH_BASE_ADDR (0x40022000)

#define FLASH_MEM_BASE_ADDR 0x8000000
#define FLASH_MEM_SIZE (1<<18)
#define FLASH_MEM_END_ADDR (FLASH_MEM_BASE_ADDR + FLASH_MEM_SIZE - 1)
#define FLASH_PAGE_SIZE (2048)

#define RTC_EXTI_LINE (20)

#define RCC_CR REGISTER(RCC_BASE_ADDR + 0x0)
#define RCC_CR_HSION (1<<0)
#define RCC_CR_HSIRDY (1<<1)
#define RCC_CR_HSEON (1<<16)
#define RCC_CR_HSERDY (1<<17)
#define RCC_CR_PLLON (1<<24)
#define RCC_CR_PLLRDY (1<<25)

#define RCC_CFGR REGISTER(RCC_BASE_ADDR + 0x4)
#define RCC_CFGR_PLLMUL_MASK (0xf<<18)
#define RCC_CFGR_PLLMUL_FACTOR (1<<18)
#define RCC_CFGR_PLLSRC_MASK (1<<16)
#define RCC_CFGR_PLLSRC_HALF_HSI (0<<16)
#define RCC_CFGR_PLLSRC_HSE_PREDIV (1<<16)
#define RCC_CFGR_USBPRE_MASK (1<<22)
#define RCC_CFGR_USBPRE_DIV_1PT5 (0<<22)
#define RCC_CFGR_USBPRE_DIV_1 (1<<22)
#define RCC_CFGR_SW_MASK (3<<0)
#define RCC_CFGR_SW_HSI (0)
#define RCC_CFGR_SW_HSE (1)
#define RCC_CFGR_SW_PLL (2)
#define RCC_CFGR_SWS_HSI (0<<2)
#define RCC_CFGR_SWS_HSE (1<<2)
#define RCC_CFGR_SWS_PLL (2<<2)
#define RCC_CFGR_SWS_MASK (3<<2)
#define RCC_CFGR_HPRE_MASK (0xf << 4)
#define RCC_CFGR_HPRE_FACTOR (1 << 4)

#define RCC_AHBENR REGISTER(RCC_BASE_ADDR + 0x14)
#define RCC_AHBENR_IOP_AEN (1<<17)
#define RCC_AHBENR_IOP_BEN (1<<18)

#define RCC_APB2ENR REGISTER(RCC_BASE_ADDR + 0x18)
#define RCC_APB2ENR_SYSCFGEN (1<<0)
#define RCC_APB2ENR_SPI1EN (1<<12)
#define RCC_APB2ENR_USART1EN (1<<14)
#define RCC_APB2ENR_SPI4EN (1<<15)

#define RCC_APB1ENR REGISTER(RCC_BASE_ADDR + 0x1C)

#define RCC_APB1ENR_SPI2EN (1<<14)
#define RCC_APB1ENR_SPI3EN (1<<15)
#define RCC_APB1ENR_USART2EN (1<<17)
#define RCC_APB1ENR_I2C1EN (1<<21)
#define RCC_APB1ENR_USBEN (1<<23)
#define RCC_APB1ENR_PWREN (1<<28)

#define RCC_BDCR REGISTER(RCC_BASE_ADDR + 0x20)
#define RCC_BDCR_RTCEN (1<<15)
#define RCC_BDCR_RTCSEL_MASK (3<<8)
#define RCC_BDCR_RTCSEL_LSI (2<<8)

#define RCC_CSR REGISTER(RCC_BASE_ADDR + 0x24)
#define RCC_CSR_LSION (1<<0)
#define RCC_CSR_LSIRDY (1<<1)

#define RTC_WKUP_IRQ 3
#define FLASH_IRQ 4
#define EXT3_IRQ 9
#define EXT4_IRQ 10
#define USB_HP_IRQ 19
#define USB_LP_IRQ 20
#define I2C1_EV_IRQ 31
#define I2C1_ERR_IRQ 32
#define SPI1_IRQ 35
#define SPI2_IRQ 36
#define USART1_IRQ 37
#define USART2_IRQ 38
#define RTC_ALARM_IRQ 41
#define USB_WAKEUP_IRQ 42

#define FLASH_ACR REGISTER(FLASH_BASE_ADDR + 0x0)
#define FLASH_KEYR REGISTER(FLASH_BASE_ADDR + 0x4)
#define FLASH_OPTKEYR REGISTER(FLASH_BASE_ADDR + 0x8)
#define FLASH_SR REGISTER(FLASH_BASE_ADDR + 0xC)
#define FLASH_SR_BUSY (1<<0)
#define FLASH_SR_EOP (1<<5)
#define FLASH_CR REGISTER(FLASH_BASE_ADDR + 0x10)
#define FLASH_CR_PG (1<<0)
#define FLASH_CR_PER (1<<1)
#define FLASH_CR_STRT (1<<6)
#define FLASH_CR_LOCK (1<<7)
#define FLASH_CR_ERRIE (1<<10)
#define FLASH_CR_EOPIE (1<<12)
#define FLASH_AR REGISTER(FLASH_BASE_ADDR + 0x14)

#define FLASH_KEYR_KEY1 0x45670123
#define FLASH_KEYR_KEY2 0xCDEF89AB

struct usb_ep;

typedef u32 usbw_t;
#define USB_EP(N) (*((volatile struct usb_ep *)(USB_PMA_ADDR + 16 * N)))

struct gpio_port;
#define PORTA ((volatile struct gpio_port *)(0x48000000))
#define PORTB ((volatile struct gpio_port *)(0x48000400))

struct usart_port;
#define USART1 ((volatile struct usart_port *)(0x40013800))
#define USART2 ((volatile struct usart_port *)(0x40004400))

struct spi_port {
	u32 cr1;
	u32 cr2;
	u32 sr;
	u32 dr;
	u32 crcpr;
	u32 rxcrcr;
	u32 txcrcr;
	u32 i2scfgr;
	u32 i2spr;
};

#define SPI1 ((volatile struct spi_port *)(0x40013000))

#define SPI_CR1_RXONLY (1<<10)
#define SPI_CR1_SSM (1<<9)
#define SPI_CR1_SSI (1<<8)
#define SPI_CR1_SPE (1<<6)
#define SPI_CR1_BR_FACTOR (1<<3)
#define SPI_CR1_BR_MASK (7<<3)
#define SPI_CR1_MSTR (1<<2)
#define SPI_CR1_CPOL (1<<1)
#define SPI_CR1_CPHA (1<<0)

#define SPI_CR2_FRXTH (1<<12)
#define SPI_CR2_DS_FACTOR (1<<8)
#define SPI_CR2_DS_MASK (0xf<<8)
#define SPI_CR2_TXEIE (1<<7)
#define SPI_CR2_RXNEIE (1<<6)
#define SPI_CR2_ERRIE (1<<5)
#define SPI_CR2_FRF (1<<4)
#define SPI_CR2_SSOE (1<<2)
#define SPI_CR2_TXDMAEN (1<<1)
#define SPI_CR2_RXDMAEN (1<<0)

#define SPI_SR_FRE (1<<8)
#define SPI_SR_BSY (1<<7)
#define SPI_SR_OVR (1<<6)
#define SPI_SR_MODF (1<<5)
#define SPI_SR_CRCERR (1<<4)
#define SPI_SR_UDR (1<<3)
#define SPI_SR_CHSIDE (1<<2)
#define SPI_SR_TXE (1<<1)
#define SPI_SR_RXNE (1<<0)

struct i2c_port;

#define I2C1 ((volatile struct i2c_port *)(0x40005400))
#define I2C2 ((volatile struct i2c_port *)(0x40005800))

#endif
