# Firmware for Signet Devices

## Prerequisites

#### Cloning Directions

This project uses git submodules, you will either need to use `git clone
--recursive <repourl>` or after cloning use `git submodule update --init`.

#### Dependencies

The following packages must be installed:
`build-essential autoconf automake gperf texinfo help2man ncurses-dev bison flex libtool-bin libjson-c-dev libiberty-dev dfu-util unzip gawk`

This has been tested in various GNU/Linux machines, in other platforms you
might need more, and possibly differently named, packages.


## Building the Signet Legacy Firmware

#### Prepare the Toolchain, Dependencies and Tools

This will build the toolchain and dependencies:
```
cd firmware
./build-deps.sh
```

To package the firmware for the Signet client GUI we need the firmware encoder:
`make signet-firmware-encoder`.

#### Build the Firmware

`make` builds the Signet firmware.

Package it with: `./signet-firmware-encoder signet-fw.bin signet-fw.sfw`


## Building the Signet HC Firmware

#### Prepare the Toolchain, Dependencies and Tools

This will build the toolchain and dependencies:
```
cd firmware-hc
./signet-hc-build-deps.sh
```

To build the Signet High-capacity firmware encoder: `make hc-firmware-encoder`

#### Build the Firmware

Signet HC firmware has too parts, they need to be built separately:
`make BT_MODE=A` and `make BT_MODE=B`.

To package Signet HC firmware, we need to specify the version number too:
`./hc-firmware-encoder signet-fw-a.bin signet-fw-b.bin signet-fw-0.2.3.sfwhc 0 2 3`

### Reproducible Builds

Both Signet and Signet High-capacity firmware uses locally compiled toolchains
(via crosstool-ng) to increase the chance of reproducible builds.
