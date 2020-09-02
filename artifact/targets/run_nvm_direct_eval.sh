#! /bin/bash

set +x

source common.sh

NVMD=$DIR/nvm-direct-prebuilt

# 1. Agamotto
rm -rf $OUTDIR/klee-nvm-direct-static
$KLEE --output-dir=$OUTDIR/klee-nvm-direct-static \
  --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false \
  $CONSTRAINTS \
  --write-errors-only=true \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $NVMD/005_AllocsOriginal.bc \
  --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. KLEE default (random-path + covnew)
rm -rf $OUTDIR/klee-nvm-direct-default
$KLEE --output-dir=$OUTDIR/klee-nvm-direct-default \
  --custom-checkers=false \
  $CONSTRAINTS \
  --write-errors-only=true \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $NVMD/005_AllocsOriginal.bc \
  --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# $KLEE --search=nvm --nvm-heuristic-type=static --write-errors-only=true 
# --max-memory=20000  --disable-verify=true --libc=uclibc --link-known-lib=libpmem -
# -posix-runtime --env-file=pmdk.env 005_AllocsNoFC.bc 
# --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 1 --sym-arg 1