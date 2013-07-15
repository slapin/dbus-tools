#!/bin/sh

autoreconf -f -i
set -e
./configure --host=arm-angstrom-gnueabi --with-libtool-sysroot=/usr/local/oecore-x86_64/sysroots/armv5te-angstrom-linux-gnueabi
make

