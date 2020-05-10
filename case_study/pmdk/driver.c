#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "btree_map.h"
#include <time.h>
#include <libpmem.h>


uint64_t getnanosec(struct timespec *start, struct timespec *end)
{
	uint64_t ret = 0;
	ret += (end->tv_sec - start->tv_sec) * 1000000000lu;
	ret += (end->tv_nsec - start->tv_nsec);
    return ret;
}


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

    printf("Getting root from pool (%p)...\n", pop);

    TOID(driver_root_t) root = POBJ_ROOT(pop, driver_root_t);
    
    assert(!TOID_IS_NULL(root) && "Root is null!");
    assert(TOID_VALID(root) && "Root is invalid!");
    printf("\troot.oid.offset: %lu\n", root.oid.off);
    printf("\troot._type: %p\n", root._type);
    //printf("\troot._type_num: %d\n", root._type_num);
    assert(D_RO(root) && "Root direct is null!");
#if 0
    printf("Checking persistent state...\n");

    TX_BEGIN(pop) {
        // assert((TOID_IS_NULL(D_RO(root)->tree) || TOID_VALID(D_RO(root)->tree)) && "Invalid thing!");
        assert(TOID_IS_NULL(D_RO(root)->tree) || TOID_VALID(D_RO(root)->tree));
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

    fprintf(stderr, "btree_map is now clean.\n");
#endif
    uint64_t total_time = 0;
    char buf[4096];
    for (uint64_t i = 0; i < n_entries; ++i) {
	    struct timespec start, end;
        uint64_t reverse = n_entries - 1 - i;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);

        pmem_memset_persist(buf, i, 4096);
        //pmem_flush(buf, 4096);
        _mm_sfence();

        //TX_BEGIN(pop) {
            //TOID(uint64_t) val = TX_NEW(uint64_t);
            //TX_ADD(val);
            //*D_RW(val) = reverse; 
            //assert(!btree_map_insert(pop, D_RO(root)->tree, reverse, val.oid));
            //TX_SET(root, num_nodes, D_RO(root)->num_nodes + 1);
        //} //TX_END
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
        total_time += getnanosec(&start, &end);
        //fprintf(stderr, "%d\n", (int)buf[0]);
        //fprintf(stderr, "Size is now %lu!\n", D_RO(root)->num_nodes);
        //assert(!btree_map_remove_free(pop, D_RO(root)->tree, reverse));
    }

    printf("Time/op: %lu nsec\n", total_time / n_entries);

#if 0
    TX_BEGIN(pop) {
        assert(!btree_map_clear(pop, D_RO(root)->tree) && "Could not clear tree!");
    } TX_END

    fprintf(stderr, "btree_map is clean again.\n");

	for (uint64_t i = 0; i < n_entries; ++i) {  
		TX_BEGIN(pop) {
			TOID(uint64_t) val = TX_NEW(uint64_t);
			TX_ADD(val);
			*D_RW(val) = i; 
			assert(!btree_map_insert(pop, D_RO(root)->tree, i, val.oid));
			TX_SET(root, num_nodes, D_RO(root)->num_nodes + 1);   
			fprintf(stderr, "Size is now %lu!\n", D_RO(root)->num_nodes);
		} TX_END
	}

    for (uint64_t i = 0; i < n_entries; ++i) {
        PMEMoid val = btree_map_get(pop, D_RO(root)->tree, i);
        uint64_t *ptr = pmemobj_direct(val);
        assert(OID_INSTANCEOF(val, uint64_t) && ptr && *ptr == i);
        fprintf(stderr, "\t%lu => %lu!\n", i, *ptr);
    }

    // want to cause btree_map_rotate_left to be invoked
    for (uint64_t i = 0; i < n_entries; ++i) {
      TX_BEGIN(pop) {
        uint64_t reverse = n_entries - 1 - i;
        btree_map_remove(pop, D_RO(root)->tree, reverse);
        TX_SET(root, num_nodes, D_RO(root)->num_nodes - 1);
      } TX_END
      fprintf(stderr, "Size is now %lu\n", D_RO(root)->num_nodes);
    }
#endif
    pmemobj_close(pop);

    return 0;
}