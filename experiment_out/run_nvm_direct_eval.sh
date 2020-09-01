#! /bin/bash
set +x

OUTDIR=../../experiment_out
cp pmdk.env ../build/bin
cd ../build/bin
ulimit -s unlimited

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-concrete-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM 10 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-concrete-default \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM 10 &

exit

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-no-fc-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 3 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-no-fc-default \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 3 &

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 3 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-default \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 3 &

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-short-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-short-default \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-nvm-direct-long-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-nvm-direct-long-default \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsOriginal.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 &
exit
# 3. BFS
./klee --output-dir=$OUTDIR/klee-nvm-direct-bfs --search=bfs \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-nvm-direct-dfs --search=dfs \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-nvm-direct-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=3600 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libpmem \
  --posix-runtime --env-file=pmdk.env \
  005_AllocsNoFC.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 4 69 &
