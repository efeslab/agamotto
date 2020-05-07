#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "btree_map.h"

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
    assert(D_RO(root) && "Root direct is null!");

    printf("Checking persistent state...\n");

    for (uint64_t i = 0; i < n_entries; ++i) {
        TX_BEGIN(pop) {
            uint64_t reverse = n_entries - 1 - i;
            TOID(uint64_t) val = TX_NEW(uint64_t);
            // TX_ADD(val);
            // iangneal: double add
            // TX_ADD(val);
            *D_RW(val) = reverse; 
            // assert(!btree_map_insert(pop, D_RO(root)->tree, reverse, val.oid));
            TX_SET(root, num_nodes, D_RO(root)->num_nodes + 1);
        } TX_END
        printf("Size is now %lu!\n", D_RO(root)->num_nodes);
    }

    // want to cause btree_map_rotate_left to be invoked
    // for (uint64_t i = 0; i < n_entries; ++i) {
    //   TX_BEGIN(pop) {
    //     uint64_t reverse = n_entries - 1 - i;
    //     btree_map_remove(pop, D_RO(root)->tree, reverse);
    //     TX_SET(root, num_nodes, D_RO(root)->num_nodes - 1);
    //   } TX_END
    //   printf("Size is now %lu\n", D_RO(root)->num_nodes);
    // }

    pmemobj_close(pop);

    return 0;
}
