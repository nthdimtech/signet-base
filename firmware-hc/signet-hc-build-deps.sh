#!/bin/bash

#
#Build toolchain in "toolchain/" folder
#
pushd ct-ng
ct-ng build
popd

#
#Environment
#
FW_SOURCE_ROOT=$PWD
TARGET_TUPLE=arm-none-eabi
TOOLCHAIN=$FW_SOURCE_ROOT/toolchain/$TARGET_TUPLE/
SYSROOT=$TOOLCHAIN/$TARGET_TUPLE
CFLAGS="-nostartfiles -ffunction-sections -fdata-sections -O2 -Os -I$SYSROOT/include -L$SYSROOT/lib"
export PATH=$TOOLCHAIN/bin:$PATH

#
# Make sysroot folders writable so we can install mini-gmp and nettle
#
mkdir -p $SYSROOT/include
chmod u+w $SYSROOT/include
mkdir -p $SYSROOT/lib
chmod u+w $SYSROOT/lib

#
# Install mini-gmp
#
pushd mini-gmp/mini-gmp
$TARGET_TUPLE-gcc $CFLAGS -c mini-gmp.c
rm libmini-gmp.a
$TARGET_TUPLE-ar -r libmini-gmp.a mini-gmp.o
cp libmini-gmp.a $SYSROOT/lib
cp mini-gmp.h $SYSROOT/include
popd

#
# Install nettle
#
mkdir -p nettle/build-hc
pushd nettle
autoconf
autoheader
pushd build-hc
../configure \
--disable-assembler \
--disable-documentation \
--disable-shared \
--disable-pic \
--enable-mini-gmp \
--enable-public-key \
--host=arm-none-eabi \
--prefix=$SYSROOT
make
make install
popd
popd
