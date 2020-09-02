#! /bin/bash
set +x

source common.sh

RECIPE_DIR=$DIR/recipe-prebuilt

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-recipe-nothread-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  $RECIPE_DIR/clht-nothread-sym-friendly.bc --sym-pmem-init-from $RECIPE_DIR/pool --sym-arg 2 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-recipe-nothread-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  $RECIPE_DIR/clht-nothread-sym-friendly.bc --sym-pmem-init-from $RECIPE_DIR/pool --sym-arg 2 &
