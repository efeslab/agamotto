#!/bin/bash

source ../common.sh

TARGET=$1
SEARCH=$2
OUTPUT=$3

SERVER_PORT=6379

# CLIENT_FILE_NAMES=clients/client-many*
# CLIENT_FILE_NAMES=clients/client-many0
# CLIENT_FILE_NAMES=clients/client-10.txt
# tcp_opts() {
# 	i=0
# 	for f in $@; do
# 		name=redis-client-$i
# 		echo -n "--tcp-client-file $name $SERVER_PORT $f "
# 		let "i += 1"
# 	done
# }
# TCP_OPTS=`tcp_opts $CLIENT_FILE_NAMES`

# CLIENT_FILE_NAME=clients/client-100.txt
# TCP_OPTS="--tcp-client-file redis-client $SERVER_PORT $CLIENT_FILE_NAME"

# TCP_SYM_SIZE=100
# TCP_OPTS="--tcp-client-sym redis-client $SERVER_PORT $TCP_SYM_SIZE"

# TCP_OPTS="--sock-handler redis_simple_concrete"
# TCP_OPTS="--sock-handler redis_symbolic"
# TCP_OPTS="--sock-handler redis_semi_symbolic"

CLIENT_FILE_NAME=clients/client-100.txt
# CLIENT_FILE_NAME=clients/client-large.txt
# BATCH_SIZE=10
# TCP_OPTS="--sock-handler redis_file --sock-args 2 $CLIENT_FILE_NAME $BATCH_SIZE"
BATCH_SIZE_MIN=1
BATCH_SIZE_MAX=200
TCP_OPTS="--sock-handler redis_file --sock-args 3 $CLIENT_FILE_NAME $BATCH_SIZE_MIN $BATCH_SIZE_MAX"


AOF_FILE_NAME=appendonly.aof
AOF_FILE_MAX_SIZE=`expr 4 "*" 1024 "*" 1024`
SYM_FILE_OPTS="--con-file-max $AOF_FILE_NAME $AOF_FILE_MAX_SIZE"

PMEM=1

if [[ $PMEM -eq 1 ]]; then
	PMEM_FILE_NAME=redis-port-$SERVER_PORT-1GB-AEP
	PMEM_FILE_SIZE=1073741824 # 1 * 1024 * 1024 * 1024
	AOFGUARD_FILE_NAME=redis-$SERVER_PORT.ag
	AOFGUARD_FILE_SIZE=536870928
	PMEM_INIT_OPTS="--sym-pmem-zeroed $PMEM_FILE_NAME $PMEM_FILE_SIZE \
		--sym-pmem-delay $AOFGUARD_FILE_NAME $AOFGUARD_FILE_SIZE" 
	# PMEM_INIT_OPTS="--sym-pmem-delay $AOFGUARD_FILE_NAME $AOFGUARD_FILE_SIZE" 
	# SYM_FILE_OPTS="$SYM_FILE_OPTS --con-file pmem/$PMEM_FILE_NAME"
	# SYM_FILE_OPTS="$SYM_FILE_OPTS --con-file pmem/$AOFGUARD_FILE_NAME"

	REDIS_CONF=redis-pmem.conf
	# SEARCH="--search=nvm --nvm-heuristic-type=static"
	# SEARCH="--search=nurs:covnew"
	# SEARCH="--search=nurs:depth"
	# SEARCH="--search=dfs"

	# SEARCH="$SEARCH --use-batching-search"
else
	PMEM_INIT_OPTS=
	REDIS_CONF=redis.conf
	# SEARCH="--search=nurs:covnew"
fi

cat > pmdk.env << EOF
PMEMOBJ_LOG_LEVEL=0
PMEM_LOG_LEVEL=0
PMEM_NO_FLUSH=0
PMEM_IS_PMEM_FORCE=1
EOF

if [[ -e appendonly.aof ]]; then
	cp appendonly.aof appendonly.aof.bk;
	rm appendonly.aof
	for f in ./pmem/*.ag; do
		cp $f $f.bk
	done
fi
touch appendonly.aof

# --link-llvm-lib=$KLEE_NVM_BUILD/nvmbugs/pmdk/install/lib/pmdk_debug/libvmmalloc.so.bc \
	# --search=bfs \
	# --log-partial-queries-early \
	# --search=nvm \
	# --nvm-heuristic-type=static \
	# --posix-debug \
	# --write-paths \
	# --very-verbose \
	# --use-query-log=all:kquery \
	# --only-output-states-covering-new \
	# --all-external-warnings \
	# --posix-debug \


	# --link-llvm-lib=build/libpmem.so.bc \
	# --link-llvm-lib=build/libjemalloc.bca \
	# --link-llvm-lib=build/libjemallocat.bca \
	# --link-llvm-lib=build/libmemkind.bca \
	# --link-llvm-lib=build/libhiredis.bca \
	# --link-llvm-lib=build/liblua.bca \
	# --link-llvm-lib=build/libaofguard.bca \
	# --link-llvm-lib=build/libnuma.bca \

mkdir -p pmem

set -x
# gdb --args $KLEE \
$KLEE \
	--output-dir=$OUTPUT \
	--disable-verify=true \
	$CONSTRAINTS \
	--libc=uclibc \
	--link-llvm-lib=build/libnuma.bca \
	--link-llvm-lib=build/libpmem.so.bc \
 	--calloc-max-unit-size=4096 \
	--posix-runtime \
	--env-file=pmdk.env \
	$SEARCH \
	--check-div-zero=false \
	--check-overshift=false \
	$TARGET \
	$SYM_FILE_OPTS \
	$PMEM_INIT_OPTS \
	$TCP_OPTS \
	$REDIS_CONF
