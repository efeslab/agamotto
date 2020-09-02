#! /bin/bash
set -x

source common.sh

MCD=$DIR/memcached-pmem-prebuilt

# Agamotto's search strategy
rm -rf $OUTDIR/klee-memcached-static 
$BUILD/bin/klee --output-dir=$OUTDIR/klee-memcached-static --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false -write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$MCD/libevent.so.bc --link-llvm-lib=$MCD/libpmem.so.bc \
  --posix-runtime \
  --env-file=$MCD/memcached.env $MCD/memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &

# KLEE's default search strategy (random-path + covnew)
rm -rf $OUTDIR/klee-memcached-default
$BUILD/bin/klee --output-dir=$OUTDIR/klee-memcached-default --search=dfs \
  --custom-checkers=false --write-errors-only=true \
  $CONSTRAINTS \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$MCD/libevent.so.bc --link-llvm-lib=$MCD/libpmem.so.bc \
  --posix-runtime \
  --env-file=$MCD/memcached.env $MCD/memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force &
