#! /bin/bash

set +x

source common.sh

################################################################################
# B-Tree eval
################################################################################

# 1. Agamotto
$KLEE --output-dir=$OUTDIR/klee-pmdk-btree-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. KLEE default (random-path + covnew)
$KLEE --output-dir=$OUTDIR/klee-pmdk-btree-default \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

################################################################################
# RB-Tree eval
################################################################################

# 1. Agamotto
$KLEE --output-dir=$OUTDIR/klee-pmdk-rbtree-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. KLEE default (random-path + covnew)
$KLEE --output-dir=$OUTDIR/klee-pmdk-rbtree-default \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

################################################################################
# Hashmap Atomic eval
################################################################################

# 1. Agamotto
$KLEE --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. KLEE default (random-path + covnew)
$KLEE --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-default \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &