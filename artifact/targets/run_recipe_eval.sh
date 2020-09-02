#! /bin/bash

set +x

source common.sh

RECIPE_DIR=$DIR/recipe-prebuilt

# 1. Agamotto
rm -rf $OUTDIR/klee-recipe-nothread-static
$KLEE --output-dir=$OUTDIR/klee-recipe-nothread-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --libcxx --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $RECIPE_DIR/clht-nothread-sym-friendly.bc --sym-pmem-init-from $RECIPE_DIR/pool --sym-arg 2 &

# 2. Default (random-path + covnew)
rm -rf $OUTDIR/klee-recipe-nothread-default
$KLEE --output-dir=$OUTDIR/klee-recipe-nothread-default \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --libcxx --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $RECIPE_DIR/clht-nothread-sym-friendly.bc --sym-pmem-init-from $RECIPE_DIR/pool --sym-arg 2 &
