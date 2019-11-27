all: signet-fw.bin

HTUPLE:=arm-none-eabi

LIBS=-lnettle -lhogweed -lmini-gmp
CFLAGS=-ffunction-sections -fdata-sections -O2 -Os
ASFLAGS=
LDFLAGS=-Wl,"--gc-sections"
LDFLAGS+=-Wl,"-Map=signet-fw.map"

CFLAGS+= -DFIRMWARE -DUSE_RAW_HID -DSTM32F733xx -DUSE_HAL_DRIVER -Wall
CFLAGS+= -Istm32f7xx -I.

LDFLAGS+= -Wl,"-Tstm32f733iekx.ld"

clean:
	rm -rf *.o *.d signet-fw signet-fw.*

%.o: %.c
	$(HTUPLE)-gcc  $(CFLAGS) $< -c -o $@
	@$(HTUPLE)-gcc  $(CFLAGS) $< -M -MF $@.d

%.o: %.S
	$(HTUPLE)-gcc $(ASFLAGS) $< -c -o $@

MCU_SOURCES = main.c \
	      flash.c \
	      stm32f7xx/system_stm32f7xx.c \
	      stm32f7xx/stm32f7xx_hal.c \
	      stm32f7xx/stm32f7xx_hal_gpio.c \
	      stm32f7xx/stm32f7xx_hal_rcc.c \
	      stm32f7xx/stm32f7xx_hal_rcc_ex.c \
	      stm32f7xx/stm32f7xx_hal_rng.c \
	      stm32f7xx_hal_msp.c \
	      stm32f7xx/stm32f7xx_hal_mmc.c \
	      stm32f7xx/stm32f7xx_ll_sdmmc.c \
	      stm32f7xx/stm32f7xx_ll_usb.c \
	      stm32f7xx/stm32f7xx_hal_flash.c \
	      stm32f7xx/stm32f7xx_hal_flash_ex.c \
	      stm32f7xx/stm32f7xx_hal_cortex.c \
	      stm32f7xx/stm32f7xx_hal_dma.c \
	      stm32f7xx/stm32f7xx_hal_pcd.c \
	      stm32f7xx/stm32f7xx_hal_pcd_ex.c \
	      stm32f7xx/stm32f7xx_hal_cryp.c \
	      stm32f7xx/stm32f7xx_hal_pwr_ex.c \
	      stm32f7xx/stm32f7xx_it.c
MCU_SOURCES_S = startup_stm32f733iekx.S

SOURCES = commands.c \
	  db.c \
	  crc.c \
	  rtc_rand.c \
	  rng.c \
	  signet_aes.c \
	  usb_raw_hid.c \
	  usbd_conf.c \
	  usbd_desc.c \
	  usbd_core.c \
	  usbd_ctlreq.c \
	  usbd_ioreq.c \
	  usbd_hid.c \
	  usbd_msc.c \
	  usbd_msc_bot.c \
	  usbd_msc_data.c \
	  usbd_msc_scsi.c \
	  buffer_manager.c \
	  $(MCU_SOURCES)

ASM_SOURCES = $(MCU_SOURCES_S)
ALL_SOURCES = $(SOURCES) $(ASM_SOURCES)

#startup.c firmware_update_state.c commands.c crc.c db.c \
#	  usb_fs_driver.c \
#	  usart.c stm_aes.c usb_serial.c usb.c usb_keyboard.c \
#	  print.c irq.c gpio.c rtc_rand.c \
#	  usb_raw_hid.c $(MCU_SOURCES)

OBJECTS = $(SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.S=.o)
DEPFILES = $(SOURCES:.c=.o.d)

signet-fw: $(OBJECTS) $(ASM_OBJECTS)
	$(HTUPLE)-gcc $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@

signet-fw.bin: signet-fw
	$(HTUPLE)-objcopy $^ -O binary $@

-include $(DEPFILES)
