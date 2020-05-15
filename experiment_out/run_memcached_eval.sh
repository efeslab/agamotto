#! /bin/bash
set +x

OUTDIR=../../experiment_out

# 1. Agamotto
./klee --output-dir=$OUTDIR/klee-memcached-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libevent --link-known-lib=libpmem \
  --posix-runtime \
  --env-file=memcached.env memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &

# 2. Default (random-path + covnew)
./klee --output-dir=$OUTDIR/klee-memcached-default \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libevent --link-known-lib=libpmem \
  --posix-runtime \
  --env-file=memcached.env memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &

# 3. BFS
./klee --output-dir=$OUTDIR/klee-memcached-bfs --search=bfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libevent --link-known-lib=libpmem \
  --posix-runtime \
  --env-file=memcached.env memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &

# 4. DFS
./klee --output-dir=$OUTDIR/klee-memcached-dfs --search=dfs \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libevent --link-known-lib=libpmem \
  --posix-runtime \
  --env-file=memcached.env memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &

# 5. Coverage new 
./klee --output-dir=$OUTDIR/klee-memcached-covnew --search=nurs:covnew \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc --link-known-lib=libevent --link-known-lib=libpmem \
  --posix-runtime \
  --env-file=memcached.env memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &
