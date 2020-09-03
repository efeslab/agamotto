#! /bin/bash

source common.sh

# PMTest: two bugs from btree
# rm -rf $OUTDIR/klee-pmdk-btree-pmtest-repro 
# $KLEE --output-dir=$OUTDIR/klee-pmdk-btree-pmtest-repro --search=nvm --nvm-heuristic-type=static \
#   $CONSTRAINTS \
#   --custom-checkers=true --write-errors-only=true \
#   --disable-verify=true --libc=uclibc \
#   --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
#   --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
#   --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
#   $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# XFDetector: two bugs from hashmap atomic
# rm -rf $OUTDIR/klee-pmdk-hashmap-atomic-pmtest-repro 
# $KLEE --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-pmtest-repro --search=nvm --nvm-heuristic-type=static \
#   $CONSTRAINTS \
#   --custom-checkers=true --write-errors-only=true \
#   --disable-verify=true --libc=uclibc \
#   --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
#   --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
#   --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
#   $BUILD/bin/005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# XFDetector: one bug from redis

cd redis-pmem-prebuilt

rm -rf $OUTDIR/klee-redis-xfd-repro

./run.sh redis-server.bc \
    "--search=dfs --custom-checkers=true" \
    "$OUTDIR/klee-redis-xfd-repro" &


    # "--search=nvm --nvm-heuristic-type=static --custom-checkers=true" \
