#! /bin/bash
set +x

OUTDIR=../../experiment_out

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-pmdk-rbtree-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-pmdk-rbtree-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-pmdk-rbtree-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-pmdk-rbtree-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-pmdk-rbtree-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  003_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &
