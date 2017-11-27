#!/bin/sh
#
# Build for OpenRISC linux
rm -rf build && \
mkdir build && \
cd build && \
cmake .. -DCMAKE_CXX_COMPILER=or1k-linux-musl-g++ -DCMAKE_C_COMPILER=or1k-linux-musl-gcc && \
make && \
cd .. && \
mkdir -p bin && \
cp build/bin/stackless bin/
