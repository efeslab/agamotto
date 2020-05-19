#! /bin/bash
set +x

OUTDIR=../../experiment_out
cp pmdk.env ../build/bin
cd ../build/bin
ulimit -s unlimited

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-nvm-direct-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-nvm-direct-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-nvm-direct-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &
