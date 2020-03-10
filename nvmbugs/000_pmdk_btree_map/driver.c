#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "btree_map.h"

// struct pmemobjpool {
// 	struct pool_hdr hdr;	/* memory pool header */

// 	/* persistent part of PMEMOBJ pool descriptor (2kB) */
// 	char layout[PMEMOBJ_MAX_LAYOUT];
// 	uint64_t lanes_offset;
// 	uint64_t nlanes;
// 	uint64_t heap_offset;
// 	uint64_t unused3;
// 	unsigned char unused[OBJ_DSC_P_UNUSED]; /* must be zero */
// 	uint64_t checksum;	/* checksum of above fields */

// 	uint64_t root_offset;

// 	/* unique runID for this program run - persistent but not checksummed */
// 	uint64_t run_id;

// 	uint64_t root_size;

// 	/*
// 	 * These flags can be set from a conversion tool and are set only for
// 	 * the first recovery of the pool.
// 	 */
// 	uint64_t conversion_flags;

// 	uint64_t heap_size;

// 	struct stats_persistent stats_persistent;

// 	char pmem_reserved[496]; /* must be zeroed */

// 	/* some run-time state, allocated out of memory pool... */
// 	void *addr;		/* mapped region */
// 	int is_pmem;		/* true if pool is PMEM */
// 	int rdonly;		/* true if pool is opened read-only */
// 	struct palloc_heap heap;
// 	struct lane_descriptor lanes_desc;
// 	uint64_t uuid_lo;
// 	int is_dev_dax;		/* true if mapped on device dax */

// 	struct ctl *ctl;	/* top level node of the ctl tree structure */
// 	struct stats *stats;

// 	struct pool_set *set;		/* pool set info */
// 	struct pmemobjpool *replica;	/* next replica */

// 	/* per-replica functions: pmem or non-pmem */
// 	persist_local_fn persist_local;	/* persist function */
// 	flush_local_fn flush_local;	/* flush function */
// 	drain_local_fn drain_local;	/* drain function */
// 	memcpy_local_fn memcpy_local; /* persistent memcpy function */
// 	memmove_local_fn memmove_local; /* persistent memmove function */
// 	memset_local_fn memset_local; /* persistent memset function */

// 	/* for 'master' replica: with or without data replication */
// 	struct pmem_ops p_ops;

// 	PMEMmutex rootlock;	/* root object lock */
// 	int is_master_replica;
// 	int has_remote_replicas;

// 	/* remote replica section */
// 	void *rpp;	/* RPMEMpool opaque handle if it is a remote replica */
// 	uintptr_t remote_base;	/* beginning of the remote pool */
// 	char *node_addr;	/* address of a remote node */
// 	char *pool_desc;	/* descriptor of a poolset */

// 	persist_remote_fn persist_remote; /* remote persist function */

// 	int vg_boot;
// 	int tx_debug_skip_expensive_checks;

// 	struct tx_parameters *tx_params;

// 	/*
// 	 * Locks are dynamically allocated on FreeBSD. Keep track so
// 	 * we can free them on pmemobj_close.
// 	 */
// 	PMEMmutex_internal *mutex_head;
// 	PMEMrwlock_internal *rwlock_head;
// 	PMEMcond_internal *cond_head;

// 	struct {
// 		struct ravl *map;
// 		os_mutex_t lock;
// 		int verify;
// 	} ulog_user_buffers;

// 	void *user_data;

// 	/* padding to align size of this structure to page boundary */
// 	/* sizeof(unused2) == 8192 - offsetof(struct pmemobjpool, unused2) */
// 	char unused2[PMEM_OBJ_POOL_UNUSED2_SIZE];
// };

typedef struct driver_root {
    size_t num_nodes;
    TOID(struct btree_map) tree;
} driver_root_t;

POBJ_LAYOUT_BEGIN(000_driver);
POBJ_LAYOUT_ROOT(000_driver, driver_root_t);
POBJ_LAYOUT_END(000_driver);

TOID_DECLARE(uint64_t, BTREE_MAP_VAL_OFFSET + 0);

int main(int argc, const char *argv[]) {

    if (argc <= 2) {
        fprintf(stderr, "%s: path n_entries\n", argv[0]);
        return -1;
    }

    uint64_t n_entries = (uint64_t)atoi(argv[2]);

    // (void*)pop is actually the mmap-ed region...
    PMEMobjpool *pop = pmemobj_create(argv[1], POBJ_LAYOUT_NAME(000_driver), PMEMOBJ_MIN_POOL, 0666);
    if (pop == NULL) {
        printf("Create failed, trying open layout %s. (%s)\n", POBJ_LAYOUT_NAME(000_driver), strerror(errno));
	    pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(000_driver));
    }

	if (pop == NULL) {
        printf("Object pool is null! (%s)\n", strerror(errno));
		exit(1);
	}

    printf("Getting root...\n");

    TOID(driver_root_t) root = POBJ_ROOT(pop, driver_root_t);
    
    assert(!TOID_IS_NULL(root) && "Root is null!");

    // {
    //     // Hack
    //     for (void *addr = (void*)pop; addr < ((void*)pop + PMEMOBJ_MIN_POOL); addr+=64) {
    //         _mm_clflush(addr);
    //     }
    //     _mm_sfence();
    // }

    printf("Checking persistent state...\n");

    TX_BEGIN(pop) {
        assert((TOID_IS_NULL(D_RO(root)->tree) || TOID_VALID(D_RO(root)->tree)) && "Invalid thing!");
        printf("\tChecking btree_map for old nodes...\n");
        if (!TOID_IS_NULL(D_RO(root)->tree) && !btree_map_is_empty(pop, D_RO(root)->tree)) {
            printf("\t+ Removing old nodes...\n");
            // (iangneal): Memory leak by not free-ing the underlying values
            assert(!btree_map_clear(pop, D_RO(root)->tree) && "Could not clear tree!");
            printf("\t+ Free-ing old allocation...\n");
            TX_FREE(D_RO(root)->tree);
        }

        printf("\tChecking btree_map for old nodes...\n");
        TX_SET(root, num_nodes, 0);
        TX_SET(root, tree, TX_ZNEW(struct btree_map));
    } TX_END

    printf("btree_map is now clean.\n");

    for (uint64_t i = 0; i < n_entries; ++i) {
        TX_BEGIN(pop) {
            TOID(uint64_t) val = TX_NEW(uint64_t);
            TX_ADD(val);
            *D_RW(val) = i; 
            assert(!btree_map_insert(pop, D_RO(root)->tree, i, val.oid));
            TX_SET(root, num_nodes, D_RO(root)->num_nodes + 1);
        } TX_END
        printf("Size is now %lu!\n", D_RO(root)->num_nodes);
    }

    for (uint64_t i = 0; i < n_entries; ++i) {
        PMEMoid val = btree_map_get(pop, D_RO(root)->tree, i);
        uint64_t *ptr = pmemobj_direct(val);
        assert(OID_INSTANCEOF(val, uint64_t) && ptr && *ptr == i);
        printf("\t%lu => %lu!\n", i, *ptr);
    }

    pmemobj_close(pop);

    return 0;
}