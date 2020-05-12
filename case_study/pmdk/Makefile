
default: driver

driver:
	gcc -I$(realpath ./pmdk/install/include) -L$(realpath ./pmdk/install/lib/pmdk_debug) -o driver driver.c btree_map_performance_fixed.c -lpmem -lpmemobj -lpthread

run: 
	rm /tmp/pmdk_test_pool
	env LD_LIBRARY_PATH=$(realpath ./pmdk/install/lib/pmdk_debug) PMEM_IS_PMEM_FORCE=1 numactl -m 0 -C 0 ./driver /tmp/pmdk_test_pool 100000

debug_run: 
	rm /tmp/pmdk_test_pool
	env LD_LIBRARY_PATH=$(realpath ./pmdk/install/lib/pmdk_debug) PMEM_LOG_LEVEL=15 PMEM_IS_PMEM_FORCE=1 PMEM_NO_FLUSH=0 numactl -m 0 -C 0 ./driver /tmp/pmdk_test_pool 1

clean:
	rm driver
