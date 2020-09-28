#! /bin/bash

source common.sh

cd redis-pmem-2-prebuilt

# 1. Klee

rm -rf $OUTDIR/klee-redis-default

./run.sh build/redis-server.bc "--search=dfs" "$OUTDIR/klee-redis-default"

# 2. Agamotto

rm -rf $OUTDIR/klee-redis-static

./run.sh build/redis-server.bc "--search=nvm --nvm-heuristic-type=static" "$OUTDIR/klee-redis-static" 
