#!/bin/bash

KLEE_NVM_BUILD=/home/bgreeves/klee-nvm/build
KLEE=$KLEE_NVM_BUILD/bin/klee

TARGET=$1

PMEM=1

if [[ $PMEM -eq 1 ]]; then
	PMEM_FILE_NAME=redis.pmem
	# PMEM_FILE_SIZE=2147483648  # 2 * 1024 * 1024 * 1024
	# PMEM_FILE_SIZE=67108864  # 64 * 1024 * 1024
	PMEM_FILE_SIZE=8388608  # 8 * 1024 * 1024
	PMEM_INIT_OPTS="--sym-pmem-init-from $PMEM_FILE_NAME"

	REDIS_CONF=redis-pmem.conf
	# SEARCH="--search=nvm --nvm-heuristic-type=static"
	# SEARCH="--search=nurs:covnew"
	SEARCH="--search=nurs:depth"
	# SEARCH="--search=dfs"

	SEARCH="$SEARCH --use-batching-search"
else
	PMEM_INIT_OPTS=
	REDIS_CONF=redis.conf
	SEARCH="--search=nurs:covnew"
fi

CLIENT_FILE_NAME=client.txt
SERVER_PORT=6379

TCP_SYM_SIZE=100
# TCP_OPTS="--tcp-client-file redis-client $SERVER_PORT $CLIENT_FILE_NAME"
# TCP_OPTS="--tcp-client-sym redis-client $SERVER_PORT $TCP_SYM_SIZE"
# TCP_OPTS="--sock-handler redis_simple_concrete"
# TCP_OPTS="--sock-handler redis_symbolic"
TCP_OPTS="--sock-handler redis_semi_symbolic"

cat > pmdk.env << EOF
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
$KLEE \
	--disable-verify=true \
	--max-memory=128000 \
	--libc=uclibc \
	--link-known-lib=libpmem \
	--link-known-lib=libpmemobj \
	--link-llvm-lib=deps/hiredis/libhiredis.bca \
	--link-llvm-lib=deps/lua/src/liblua.bca \
	--posix-runtime \
	--env-file=pmdk.env \
	$SEARCH \
	--check-div-zero=false \
	--check-overshift=false \
	--use-query-log=all:kquery \
	--only-output-states-covering-new \
	$TARGET \
	$PMEM_INIT_OPTS \
	$TCP_OPTS \
	$REDIS_CONF
