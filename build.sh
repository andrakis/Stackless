#!/bin/sh
#
# Build for Linux
rm -rf build && \
mkdir -p build && \
cd build && \
cmake .. && \
make && \
cd .. && \
mkdir -p bin && \
cp build/bin/stackless bin/
