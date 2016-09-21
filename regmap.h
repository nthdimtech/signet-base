#ifndef REGMAP_H
#define REGMAP_H

#include "types.h"

#define REGISTER(addr) (*((volatile u32 *)(addr)))
#define REGISTER16(addr) (*((volatile u16 *)(addr)))

#if defined(MCU_STM32L443XC)
#include "regmap_l443xc.h"
#elif defined(MCU_STM32F303XC)
#include "regmap_f303xc.h"
#else
#error Undefined MCU
#endif

#define NVIC_ISERn(N) REGISTER(0xE000E100 + 4 * (N))
#define NVIC_ICERn(N) REGISTER(0xE000E180 + 4 * (N))
#define NVIC_ISPRn(N) REGISTER(0xE000E200 + 4 * (N))
#define NVIC_ICPRn(N) REGISTER(0xE000E280 + 4 * (N))
#define NVIC_IPRn(N) REGISTER(0xE000E400 + 4 * (N))

#define SCB_BASE_ADDR 0xE000ED00
#define SCB_VTOR REGISTER(SCB_BASE_ADDR + 0x8)
#define SCB_AIRCR REGISTER(SCB_BASE_ADDR + 0xC)
#define SCB_AIRCR_VECTKEY (0x5FA0000)
#define SCB_AIRCR_SYSRESETREQ (1<<2)
#define SCB_SHPR2 REGISTER(SCB_BASE_ADDR + 0x1C)
#define SCB_SHPR3 REGISTER(SCB_BASE_ADDR + 0x20)
#define SCB_SHCSR REGISTER(SCB_BASE_ADDR + 0x24)
#define SCB_HFSR REGISTER(SCB_BASE_ADDR + 0x2C)
#define SCB_BFAR REGISTER(SCB_BASE_ADDR + 0x38)

#define STK_BASE_ADDR 0xE000E010
#define STK_CSR REGISTER(STK_BASE_ADDR + 0x0)
#define STK_RVR REGISTER(STK_BASE_ADDR + 0x4)
#define STK_CVR REGISTER(STK_BASE_ADDR + 0x8)

#define USB_CNTR REGISTER(USB_BASE_ADDR + 0x40)
#define USB_CNTR_FRES (1)
#define USB_CNTR_RESETM (1<<10)
#define USB_CNTR_ERRM (1<<13)
#define USB_CNTR_PMAOVRM (1<<14)
#define USB_CNTR_CTRM (1<<15)

#define USB_ISTR REGISTER(USB_BASE_ADDR + 0x44)
#define USB_ISTR_CTR (1<<15)
#define USB_ISTR_PMAOVR (1<<14)
#define USB_ISTR_ERR (1<<13)
#define USB_ISTR_RESET (1<<10)
#define USB_ISTR_DIR (1<<4)
#define USB_ISTR_ID_MASK (0xf)

#define USB_DADDR REGISTER(USB_BASE_ADDR + 0x4C)
#define USB_DADDR_EN (1<<7)
#define USB_BTABLE REGISTER(USB_BASE_ADDR + 0x50)

struct usb_ep {
	usbw_t tx_addr;
	usbw_t tx_count;
	usbw_t rx_addr;
	usbw_t rx_count;
};
#define BTABLE_SIZE 64

#define USB_EPR(N) ((volatile u32 *)(USB_BASE_ADDR))[N]
#define USB_EPR_CTR_RX (1<<15)
#define USB_EPR_SETUP (1<<11)
#define USB_EPR_CTR_TX (1<<7)
#define USB_EPR_TYPE_MASK (3<<9)
#define USB_EPR_TYPE_BULK (0<<9)
#define USB_EPR_TYPE_CONTROL (1<<9)
#define USB_EPR_TYPE_ISO (2<<9)
#define USB_EPR_TYPE_INTERRUPT (3<<9)
#define USB_EPR_KIND_MASK (1<<8)
#define USB_EPR_KIND_SINGLE_BUFFER (0<<8)
#define USB_EPR_KIND_DOUBLE_BUFFER (1<<8)
#define USB_EPR_KIND_NO_STATUS_OUT (0<<8)
#define USB_EPR_KIND_STATUS_OUT (1<<8)
#define USB_EPR_STAT_RX_MASK (3<<12)
#define USB_EPR_STAT_RX_DISABLED (0<<12)
#define USB_EPR_STAT_RX_STALL (1<<12)
#define USB_EPR_STAT_RX_NAK (2<<12)
#define USB_EPR_STAT_RX_VALID (3<<12)
#define USB_EPR_STAT_TX_MASK (3<<4)
#define USB_EPR_STAT_TX_DISABLED (0<<4)
#define USB_EPR_STAT_TX_STALL (1<<4)
#define USB_EPR_STAT_TX_NAK (2<<4)
#define USB_EPR_STAT_TX_VALID (3<<4)
#define USB_EPR_EA_MASK (0xf)

#define USB_EP_RX_COUNT(N) (((N) < 32) ? (N) : ((1<<15) | ((((N)/32)-1)<<10)))

struct gpio_port {
	u32 MODER;
	u32 OTYPER;
	u32 OSPEEDR;
	u32 PUPDR;
	u32 IDR;
	u32 ODR;
	u32 BSRR;
	u32 LCKR;
	u32 AFRL;
	u32 AFRH;
	u32 BRR;
};

#define USART_CR1_UE 0x1
#define USART_CR1_RE 0x4
#define USART_CR1_TE 0x8
#define USART_CR1_PCE 0x200
#define USART_CR1_PS 0x100
#define USART_CR1_TXEIE 0x80

#define USART_ISR_TC 0x40
#define USART_ICR_TCCF 0x40

struct usart_port {
	u32 CR1;
	u32 CR2;
	u32 CR3;
	u32 BRR;
	u32 GTPR;
	u32 RTOR;
	u32 RQR;
	u32 ISR;
	u32 ICR;
	u32 RDR;
	u32 TDR;
};

struct i2c_port {
	u32 cr1;
	u32 cr2;
	u32 oar1;
	u32 oar2;
	u32 timingr;
	u32 timeoutr;
	u32 isr;
	u32 icr;
	u32 pecr;
	u32 rxdr;
	u32 txdr;
};

#define I2C_CR2_RD_WRN (1<<10)
#define I2C_CR2_START (1<<13)
#define I2C_CR2_STOP (1<<14)
#define I2C_CR2_NACK (1<<15)
#define I2C_CR2_AUTOEND (1<<25)
#define I2C_CR2_NBYTES_FACTOR (1<<16)
#define I2C_CR2_NBYTES_MASK (0xff0000)
#define I2C_CR2_SADDR_MASK (0x3ff)

#define I2C_CR1_PE (1)
#define I2C_CR1_TXIE (1<<1)
#define I2C_CR1_RXIE (1<<2)
#define I2C_CR1_DNF_FACTOR (1<<8)
#define I2C_CR1_ANF_OFF (1<<12)

#define I2C_ISR_TXE (1)
#define I2C_ISR_TXIS (1<<1)
#define I2C_ISR_RXNE (1<<2)
#define I2C_ISR_NACKF (1<<4)
#define I2C_ISR_STOPF (1<<5)
#define I2C_ISR_TC (1<<6)

#define I2C_ICR_STOPCF (1<<5)

#define EXTI_IMR REGISTER(EXTI_BASE_ADDR + 0x0)
#define EXTI_EMR REGISTER(EXTI_BASE_ADDR + 0x4)
#define EXTI_RTSR REGISTER(EXTI_BASE_ADDR + 0x8)
#define EXTI_FTSR REGISTER(EXTI_BASE_ADDR + 0xC)
#define EXTI_PR REGISTER(EXTI_BASE_ADDR + 0x14)

#define RTC_CR REGISTER(RTC_BASE_ADDR + 8)
#define RTC_ISR REGISTER(RTC_BASE_ADDR + 0xC)
#define RTC_ISR_WUTWF (1<<2)
#define RTC_ISR_INIT (1<<7)
#define RTC_ISR_WUTF (1<<10)
#define RTC_CR_OSEL_MASK (3 << 21)
#define RTC_CR_OSEL_WAKEUP (3 << 21)
#define RTC_CR_TSIE (1 << 15)
#define RTC_CR_WUTIE (1 << 14)
#define RTC_CR_WUTE (1 << 10)
#define RTC_WUTR REGISTER(RTC_BASE_ADDR + 0x14)
#define RTC_WPR REGISTER(RTC_BASE_ADDR + 0x24)
#define RTC_WPR_KEY1 (0xCA)
#define RTC_WPR_KEY2 (0x53)

#define PWR_CR REGISTER(PWR_BASE_ADDR + 0)
#define PWR_CR_DPB (1<<8)

#endif
