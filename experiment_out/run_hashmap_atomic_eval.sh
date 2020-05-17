#! /bin/bash
set +x

OUTDIR=../../experiment_out

cd ../build/bin
ulimit -s unlimited

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmemobj --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &
