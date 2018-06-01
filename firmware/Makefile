all: signet-fw.dfu

BTUPLE:=$(shell echo $(shell ./config.guess) | sed -e 's/\([a-z_09A-Z]\)*-/\1-build_/')
HTUPLE:=arm-none-eabi

LIBBDF_INSTALLDIR=$(HOME)/x-tools/$(HTUPLE)/$(BTUPLE)/$(HTUPLE)

HOST_CFLAGS=-I.. -std=c99 -D_DEFAULT_SOURCE

SERIAL_LOADER_LDFLAGS=-L$(LIBBDF_INSTALLDIR)/lib
SERIAL_LOADER_INCLUDES=-I$(LIBBDF_INSTALLDIR)/include
SERIAL_LOADER_LIBS=-lbfd -lz -ldl -liberty
SERIAL_LOADER_CPPFLAGS=-DPACKAGE
 
serial-loader: serial-loader.c
	gcc $< $(HOST_CFLAGS) $(SERIAL_LOADER_LDFLAGS) $(SERIAL_LOADER_INCLUDES) $(SERIAL_LOADER_CPPFLAGS) $(SERIAL_LOADER_LIBS) -o $@

FIRMWARE_ENCODER_LDFLAGS=-L$(LIBBDF_INSTALLDIR)/lib
FIRMWARE_ENCODER_INCLUDES=-I$(LIBBDF_INSTALLDIR)/include -Ilibb64-1.2.1/include
FIRMWARE_ENCODER_LIBS=-lbfd -lz -ldl -ljson-c -liberty libb64-1.2.1/src/libb64.a
FIRMWARE_ENCODER_CPPFLAGS=-DPACKAGE

signet-firmware-encoder: signet-firmware-encoder.c
	gcc $< $(HOST_CFLAGS) $(FIRMWARE_ENCODER_LDFLAGS) $(FIRMWARE_ENCODER_INCLUDES) $(FIRMWARE_ENCODER_CPPFLAGS) $(FIRMWARE_ENCODER_LIBS) -o $@

LIBS=-lnettle
CFLAGS=-mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections -O2 -I..
LDFLAGS=-Wl,"--gc-sections"

CFLAGS+= -DFIRMWARE -Wall

#CFLAGS+= -DFACTORY_MODE
#CFLAGS+= -DTESTING_MODE

LDFLAGS+= -Wl,"-Tstm32l443xc.ld"

CFLAGS += -DUSE_RAW_HID -I../signet-desktop-client/common

clean:
	rm -rf *.o *.d signet-fw signet-fw.* serial-loader signet-firmware-encoder dfu-util-loader

%.o: %.c
	$(HTUPLE)-gcc  $(CFLAGS) $< -c -o $@
	@$(HTUPLE)-gcc  $(CFLAGS) $< -M -MF $@.d

MCU_SOURCES = ivt_l443xc.c main_l443xc.c rng_driver.c flash_l443xc.c

SOURCES = startup.c firmware_update_state.c commands.c crc.c db.c \
	  usb_fs_driver.c \
	  usart.c stm_aes.c usb_serial.c usb.c usb_keyboard.c \
	  print.c irq.c gpio.c rtc_rand.c \
	  usb_raw_hid.c $(MCU_SOURCES)

OBJECTS = $(SOURCES:.c=.o)
DEPFILES = $(SOURCES:.c=.o.d)

signet-fw: $(OBJECTS)
	$(HTUPLE)-gcc $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@
	$(HTUPLE)-objcopy --add-section version=firmware-version --set-section-flags version=noload,readonly $@ $@

signet-fw.bin: signet-fw
	$(HTUPLE)-objcopy $^ -O binary $@

signet-fw.dfu: signet-fw.bin
	cp $^ $@
	dfu-suffix -v 0x0483 -p 0xdf11 -a $@

-include $(DEPFILES)
