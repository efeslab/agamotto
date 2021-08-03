#!/bin/bash

CXXFLAGS="-msse -mclwb"

cmake \
  -DENABLE_SOLVER_STP=ON \
  -DENABLE_POSIX_RUNTIME=ON \
  -DENABLE_KLEE_UCLIBC=ON \
  -DKLEE_UCLIBC_PATH=/home/cursed/klee-uclibc \
  -DENABLE_UNIT_TESTS=ON \
  -DGTEST_SRC_DIR=/home/cursed/googletest-release-1.7.0 \
  -DLLVM_CONFIG_BINARY=/usr/bin/llvm-config-6.0 \
  -DLLVMCC=/usr/bin/clang-6.0 \
  -DLLVMCXX=/usr/bin/clang++-6.0 \
  /home/cursed/klee
