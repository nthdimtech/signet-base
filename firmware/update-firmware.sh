#!/bin/sh
dfu-util -a 0 -s 0x8000000 -D signet-fw.dfu
