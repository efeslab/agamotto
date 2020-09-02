#! /bin/bash

# KLEE_NVM_BUILD=../
# KLEE=$(realpath ./klee)

source common.sh

REDISD=$DIR/redis-pmem-prebuilt

PMEM=1

if [[ $PMEM -eq 1 ]]; then
	PMEM_FILE_NAME=$REDISD/redis.pmem
	# PMEM_FILE_SIZE=2147483648  # 2 * 1024 * 1024 * 1024
	# PMEM_FILE_SIZE=67108864  # 64 * 1024 * 1024
	PMEM_FILE_SIZE=8388608  # 8 * 1024 * 1024
	PMEM_INIT_OPTS="--sym-pmem-init-from $PMEM_FILE_NAME"

	REDIS_CONF=$REDISD/redis-pmem.conf
	SEARCH="--search=nvm --nvm-heuristic-type=static"
	# SEARCH="--search=nurs:covnew"
	# SEARCH="--search=nurs:depth"
	# SEARCH="--search=dfs"

	# REDIS_OUT=$OUTDIR/klee-redis-depth
	REDIS_OUT=$OUTDIR/klee-redis-static

	# SEARCH="$SEARCH --use-batching-search"
else
	PMEM_INIT_OPTS=
	REDIS_CONF=$REDISD/redis.conf
	SEARCH="--search=nurs:covnew"
fi

CLIENT_FILE_NAME=$REDISD/client.txt
SERVER_PORT=6379

TCP_SYM_SIZE=100
TCP_OPTS="--tcp-client-file redis-client $SERVER_PORT $CLIENT_FILE_NAME"
# TCP_OPTS="--tcp-client-sym redis-client $SERVER_PORT $TCP_SYM_SIZE"
# TCP_OPTS="--sock-handler redis_simple_concrete"
# TCP_OPTS="--sock-handler redis_symbolic"
#TCP_OPTS="--sock-handler redis_semi_symbolic"

cat > $REDISD/pmdk.env << EOF
PMEMOBJ_LOG_LEVEL=0
PMEM_LOG_LEVEL=0
PMEM_NO_FLUSH=0
PMEM_IS_PMEM_FORCE=1
EOF

# --link-llvm-lib=$KLEE_NVM_BUILD/nvmbugs/pmdk/install/lib/pmdk_debug/libvmmalloc.so.bc \
	# --search=bfs \
	# --all-external-warnings \
	# --log-partial-queries-early \
	# --search=nvm \
	# --nvm-heuristic-type=static \
	# --output-module \
	# --posix-debug \
	# --write-paths \
	# --very-verbose \
set -x

rm -rfv $REDIS_OUT

$KLEE \
	$SEARCH \
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
	$REDIS_CONF
