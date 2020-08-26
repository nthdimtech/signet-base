# HC Firmware building

#### Cloning directions

This project uses git submodules, you will either need to use `git clone --recursive <repourl>` or after cloning use `git submodule init && git submodule update`.

#### Prepare the toolchain, dependencies and tools

These need to be done only once. Build the toolchain and dependencies: `signet-hc-build-deps.sh` Build firmware encoder: `make hc-firmware-encoder`

#### Build the Firmware

`PATH=$PWD/toolchain/arm-none-eabi/bin:$PATH` add the ARM toolcahin to the PATH or have this in your .bashrc

`make BT_MODE=A` and `make BT_MODE=B` will build the firmware parts.

`./hc-firmware-encoder signet-fw-a.bin signet-fw-b.bin signet-fw-0.2.3.sfwhc 0 2 3` prepares the versioned firmware image.
