#! /bin/bash
set +x

OUTDIR=../../experiment_out

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-recipe-nothread-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread.bc --sym-pmem-init-from pool --sym-arg 1 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-recipe-nothread-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread.bc --sym-pmem-init-from pool --sym-arg 1 &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-recipe-nothread-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread.bc --sym-pmem-init-from pool --sym-arg 1 &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-recipe-nothread-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread.bc --sym-pmem-init-from pool --sym-arg 1 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-recipe-nothread-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --libcxx --posix-runtime --env-file=pmdk.env \
  clht-nothread.bc --sym-pmem-init-from pool --sym-arg 1 &
