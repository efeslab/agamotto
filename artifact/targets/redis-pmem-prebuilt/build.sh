#!/bin/bash

export LLVM_COMPILER=clang
export LLVM_COMPILER_PATH=/usr/lib/llvm-8/bin
set -x
make CC=wllvm \
	USE_PMDK=yes \
	USE_JEMALLOC=no \
	OPTIMIZATION="-O0" \
	STD=-std=gnu99 \
	CFLAGS="-fno-inline-functions" -j10
extract-bc deps/hiredis/libhiredis.a
extract-bc deps/lua/src/liblua.a
extract-bc src/redis-server
