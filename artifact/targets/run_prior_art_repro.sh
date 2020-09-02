#! /bin/bash

source common.sh

# PMTest: two bugs from btree
$KLEE --output-dir=$OUTDIR/klee-pmdk-btree-pmtest-repro --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=true --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/000_Buggy.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

exit

# XFDetector: two bugs from hashmap atomic

$KLEE --output-dir=$OUTDIR/klee-pmdk-hashmap-atomic-pmtest-repro --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=true --max-time=86400 --write-errors-only=true --max-memory=10000  \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_DIR/pmdk.env \
  $BUILD/bin/005_Clean.bc --sym-pmem-delay PMEM 8388608 PMEM --sym-arg 2 &

# XFDetector: one bug from redis

REDISD=$DIR/redis-pmem-prebuilt

PMEM_FILE_NAME=$REDISD/redis.pmem
# PMEM_FILE_SIZE=2147483648  # 2 * 1024 * 1024 * 1024
# PMEM_FILE_SIZE=67108864  # 64 * 1024 * 1024
PMEM_FILE_SIZE=8388608  # 8 * 1024 * 1024
PMEM_INIT_OPTS="--sym-pmem-init-from $PMEM_FILE_NAME"

REDIS_CONF=$REDISD/redis-pmem.conf
SEARCH="--search=nvm --nvm-heuristic-type=static"

REDIS_OUT=$OUTDIR/klee-redis-xfd-repro


CLIENT_FILE_NAME=$REDISD/client.txt
SERVER_PORT=6379

TCP_SYM_SIZE=100
TCP_OPTS="--tcp-client-file redis-client $SERVER_PORT $CLIENT_FILE_NAME"

cat > $REDISD/pmdk.env << EOF
PMEMOBJ_LOG_LEVEL=0
PMEM_LOG_LEVEL=0
PMEM_NO_FLUSH=0
PMEM_IS_PMEM_FORCE=1
EOF

rm -rfv $REDIS_OUT

$KLEE \
	$SEARCH \
    --custom-checkers=true \
	--disable-verify=true \
	--max-memory=128000 \
	--max-time=300 \
	--output-dir=$REDIS_OUT \
	--libc=uclibc \
	--link-known-lib=libpmem \
	--link-known-lib=libpmemobj \
	--link-llvm-lib=$REDISD/libpmem.so.bc \
	--link-llvm-lib=$REDISD/libpmemobj.so.bc \
	--link-llvm-lib=$REDISD/libhiredis.bca \
	--link-llvm-lib=$REDISD/liblua.bca \
  	$REDISD/redis-server.bc \
	--posix-runtime \
	--env-file=$REDISD/pmdk.env \
	$PMEM_INIT_OPTS \
	$TCP_OPTS \
	$REDIS_CONF & 
