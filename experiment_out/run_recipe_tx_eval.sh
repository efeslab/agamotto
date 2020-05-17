#! /bin/bash
set +x

OUTDIR=../../experiment_out

cp clht-nothread-transaction.bc ../build/bin
cp pool ../build/bin
cp pmdk.env ../build/bin
cd ../build/bin
ulimit -s unlimited

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-recipe-tx-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread-transaction.bc --sym-pmem-init-from pool  --sym-arg 2 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-recipe-tx-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread-transaction.bc --sym-pmem-init-from pool  --sym-arg 2 &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-recipe-tx-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread-transaction.bc --sym-pmem-init-from pool  --sym-arg 2 &


# 4. DFS
./klee --output-dir=$OUTDIR/klee-recipe-tx-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread-transaction.bc --sym-pmem-init-from pool  --sym-arg 2 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-recipe-tx-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread-transaction.bc --sym-pmem-init-from pool  --sym-arg 2 &
