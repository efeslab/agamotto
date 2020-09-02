#! /bin/bash
set +x

source common.sh

# 1. Agamotto
$KLEE --output-dir=$OUTDIR/klee-pmdk-btree-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. KLEE default (random-path + covnew)
$KLEE --output-dir=$OUTDIR/klee-pmdk-btree-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

