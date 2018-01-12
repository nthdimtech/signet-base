MEMORY
{
RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 64K
STORAGE (r) : ORIGIN = 0x8010000, LENGTH = 192K
}

SECTIONS
{

_flash_origin = ORIGIN(FLASH);

.data : AT (ORIGIN(FLASH))
{
_data_begin = .;
_ivt_ram = .;
KEEP(*(.interrupt_vector))
*(.data .data*)
usb.o(.text .text* .rodata .rodata*)
usb_fs_driver.o(.text .text* .rodata .rodata*)
usb_serial.o(.text .text* .rodata .rodata*)
usb_raw_hid.o(.text .text* .rodata .rodata*)
usb_keyboard.o(.text .text* .rodata .rodata*)
usb_storage.o(.text .text* .rodata .rodata*)
firmware_update_state.o(.text .text* .rodata .rodata*)
usart.o(.text .text* .rodata .rodata*)
print.o(.text .text* .rodata .rodata*)
flash_l443xc.o(.text .text* .rodata .rodata*)
main_l443xc.o(.text .text* .rodata .rodata*)
commands.o(.text .text* .rodata .rodata*)
rtc_rand.o(.text .text* .rodata .rodata*)
rng_driver.o(.text .text* .rodata .rodata*)
mem.o(.text .text* .rodata .rodata*)
gpio.o(.text .text* .rodata .rodata*)
irq.o(.text .text* .rodata .rodata*)
. = ALIGN(16);
_data_end = .;
} > RAM

.text _flash_origin + SIZEOF(.data) :
{
. = ALIGN(16);
*(.text .text* .rodata .rodata*)
. = ALIGN(4);
} > FLASH

.bss :
{
_bss_begin = .;
__bss_start__ = _bss_begin;
*(.bss)
*(.bss*)
*(COMMON)
. = ALIGN(4);
_bss_end = .;
__bss_end__ = _bss_end;
} > RAM

_stack_size = 1024;
_stack_end = ORIGIN(RAM)+LENGTH(RAM);
_stack_begin = _stack_end - _stack_size;
. = _stack_begin;
._stack :
{
. = . + _stack_size;
} > RAM

.special :
{
_root_page = .;
} > STORAGE

}


ENTRY(handler_reset)
