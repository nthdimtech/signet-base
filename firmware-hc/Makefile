all: signet-fw.bin

HTUPLE:=arm-none-eabi

LIBS=-lnettle -lhogweed -lmini-gmp
CFLAGS=-ffunction-sections -fdata-sections -O2 -Os -I../..
ASFLAGS=
LDFLAGS=-Wl,"--gc-sections"

CFLAGS+= -DFIRMWARE -Wall

LDFLAGS+= -Wl,"-Tstm32f733iekx.ld"

CFLAGS += -DUSE_RAW_HID -I../../signet-desktop-client/common

CFLAGS += -DSTM32F733xx

clean:
	rm -rf *.o *.d signet-fw signet-fw.*

%.o: %.c
	$(HTUPLE)-gcc  $(CFLAGS) $< -c -o $@
	@$(HTUPLE)-gcc  $(CFLAGS) $< -M -MF $@.d

%.o: %.S
	$(HTUPLE)-gcc $(ASFLAGS) $< -c -o $@

MCU_SOURCES = main.c system_stm32f7xx.c 
MCU_SOURCES_S = startup_stm32f733iekx.S

SOURCES = $(MCU_SOURCES)
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
