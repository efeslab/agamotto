#! /bin/bash

source common.sh

cd redis-pmem-2-prebuilt

# 1. Agamotto

rm -rf $OUTDIR/klee-redis-static

./run.sh build/redis-server.bc "--search=nvm --nvm-heuristic-type=static" "$OUTDIR/klee-redis-static" &

# 2. Klee

rm -rf $OUTDIR/klee-redis-default

./run.sh build/redis-server.bc "--search=dfs" "$OUTDIR/klee-redis-default" &