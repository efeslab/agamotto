#! /bin/bash
set +x

source common.sh

MCD=$DIR/memcached-pmem-prebuilt

rm -rf $OUTDIR/klee-memcached-static-fast

# Agamotto's search strategy
$BUILD/bin/klee --output-dir=$OUTDIR/klee-memcached-static-fast \
  --search=nvm --nvm-heuristic-type=static --enable-mmap-aa \
  --custom-checkers=false --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$MCD/libevent.so.bc --link-llvm-lib=$MCD/libpmem.so.bc \
  --posix-runtime \
  --env-file=$MCD/memcached.env $MCD/memcached.bc --sym-pmem-delay PMEM 8388608 --sock-handler memcached_rand \
  -m 0 -U 0 -t 1 -A -o pslab_file=PMEM,pslab_size=8,pslab_force
