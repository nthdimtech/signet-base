BT_MODE?=A
ifeq ($(BT_MODE), A)
TARGET_BASE_NAME=signet-fw-a
else
TARGET_BASE_NAME=signet-fw-b
endif

TARGET_BIN=$(TARGET_BASE_NAME).bin
TARGET_ELF=$(TARGET_BASE_NAME)
TARGET_MAP=$(TARGET_BASE_NAME).map

all: $(TARGET_BIN)

HTUPLE:=arm-none-eabi

LIBS=-lnettle -lhogweed -lmini-gmp
CFLAGS=-ffunction-sections -fdata-sections -O2 -Os
ASFLAGS=
LDFLAGS=-Wl,"--gc-sections"
LDFLAGS+=-Wl,"-Map=$(TARGET_MAP)"

CFLAGS+= -DFIRMWARE -DUSE_RAW_HID -DSTM32F733xx -DUSE_HAL_DRIVER -Wall
CFLAGS+= -Istm32f7xx -I.

ifeq ($(BT_MODE), A)
LDFLAGS+= -Wl,"-Tstm32f733iekx_a.ld"
CFLAGS+= -DBOOT_MODE_A
else
LDFLAGS+= -Wl,"-Tstm32f733iekx_b.ld"
CFLAGS+= -DBOOT_MODE_B -DENABLE_FIDO2
endif

clean:
	rm -rf *.o? *.d \
		fido2/*.o? fido2/*.d \
		fido2/extensions/*.o? fido2/extensions/*.d \
		tinycbor/*.o? tinycbor/*.d \
		signet-fw-a signet-fw-b \
		signet-fw-a.* signet-fw-b.*

ifeq ($(BT_MODE), A)
%.oa: %.c
	$(HTUPLE)-gcc  $(CFLAGS) $< -c -o $@
	@$(HTUPLE)-gcc  $(CFLAGS) $< -M -MF $@.d

%.oa: %.S
	$(HTUPLE)-gcc $(ASFLAGS) $< -c -o $@
else
%.ob: %.c
	$(HTUPLE)-gcc  $(CFLAGS) $< -c -o $@
	@$(HTUPLE)-gcc  $(CFLAGS) $< -M -MF $@.d

%.ob: %.S
	$(HTUPLE)-gcc $(ASFLAGS) $< -c -o $@
endif

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

ifeq ($(BT_MODE), B)

CFLAGS += -Itinycbor -Ifido2 -Ifido2/extensions
CFLAGS += -DENABLE_U2F

SOURCES+= tinycbor/cborencoder.c \
	  tinycbor/cborencoder_close_container_checked.c \
	  tinycbor/cborparser.c \
	  tinycbor/cborparser_dup_string.c \
	  tinycbor/cborpretty.c \
	  tinycbor/cborpretty_stdio.c \
	  tinycbor/cbortojson.c \
	  tinycbor/cborvalidation.c \
	  tinycbor/open_memstream.c

SOURCES+= fido2/crypto.c \
	  fido2/ctap.c \
	  fido2/ctap_parse.c \
	  fido2/ctaphid.c \
	  fido2/data_migration.c \
	  fido2/u2f.c \
	  fido2/util.c \
	  fido2/extensions/extensions.c
endif

ASM_SOURCES = $(MCU_SOURCES_S)
ALL_SOURCES = $(SOURCES) $(ASM_SOURCES)
ifeq ($(BT_MODE), A)
OBJECTS = $(SOURCES:.c=.oa)
ASM_OBJECTS = $(ASM_SOURCES:.S=.oa)
DEPFILES = $(SOURCES:.c=.oa.d)
else
OBJECTS = $(SOURCES:.c=.ob)
ASM_OBJECTS = $(ASM_SOURCES:.S=.ob)
DEPFILES = $(SOURCES:.c=.ob.d)
endif

$(TARGET_ELF): $(OBJECTS) $(ASM_OBJECTS)
	$(HTUPLE)-gcc $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET_BIN): $(TARGET_ELF)
	$(HTUPLE)-objcopy $^ -O binary $@

-include $(DEPFILES)
