ARM_NONE_TOOLCHAIN=$HOME/x-tools/arm-none-eabi
ARM_NONE_SYSROOT=$ARM_NONE_TOOLCHAIN/arm-none-eabi/
git submodule init &&
git submodule update &&
cd crosstool-ng &&
./bootstrap &&
./configure &&
make &&
sudo make install &&
cd .. &&
cd ct-ng &&
ct-ng build &&
cd .. &&
. setenv.sh &&
cd nettle &&
autoconf &&
autoheader &&
CFLAGS="-nostdlib -mthumb -mcpu=cortex-m4" ./configure --disable-documentation --disable-assembler --disable-shared --disable-pic --host=arm-none-eabi --prefix=$ARM_NONE_SYSROOT &&
make &&
make install &&
cd ..
