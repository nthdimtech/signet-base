#!/bin/bash

#
# Make common ct-ng if needed
#
if [ ! -e ../crosstool-ng/ct-ng ]; then
    pushd ../crosstool-ng &&
    ./bootstrap &&
    ./configure --enable-local &&
    make || exit -1
    popd
fi

#
#Build toolchain in "toolchain/" folder
#
pushd ct-ng
../../crosstool-ng/ct-ng build || exit -1
popd

ARM_NONE_TOOLCHAIN=$PWD/toolchain/arm-none-eabi
ARM_NONE_SYSROOT=$ARM_NONE_TOOLCHAIN/arm-none-eabi
export PATH=$ARM_NONE_TOOLCHAIN/bin:$PATH

#
# Make sysroot folders writable so we can install nettle
#
chmod u+w $ARM_NONE_SYSROOT/include $ARM_NONE_SYSROOT/lib

#
# Install nettle
#
cd nettle &&
autoconf &&
autoheader &&
CFLAGS="-nostdlib -mthumb -mcpu=cortex-m4" ./configure --disable-documentation --disable-assembler --disable-shared --disable-pic --host=arm-none-eabi --prefix=$ARM_NONE_SYSROOT &&
make &&
make install &&
echo "Toolchain and dependecies are ready!"
cd ..
