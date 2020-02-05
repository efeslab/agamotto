#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

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
    PMEMobjpool *pop = pmemobj_create(argv[1], POBJ_LAYOUT_NAME(000_driver), PMEMOBJ_MIN_POOL, 0666);
    if (pop == NULL) {
	    pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(000_driver));
    }

	if (pop == NULL) {
		perror(argv[1]);
		exit(1);
	}

    printf("Getting root...\n");

    TOID(driver_root_t) root = POBJ_ROOT(pop, driver_root_t);
    
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

    for (uint64_t i = 0; i < 100; ++i) {
        TX_BEGIN(pop) {
            TOID(uint64_t) val = TX_NEW(uint64_t);
            TX_ADD(val);
            *D_RW(val) = i; 
            assert(!btree_map_insert(pop, D_RO(root)->tree, i, val.oid));
            TX_SET(root, num_nodes, D_RO(root)->num_nodes + 1);
        } TX_END
        printf("Size is now %lu!\n", D_RO(root)->num_nodes);
    }

    for (uint64_t i = 0; i < 100; ++i) {
        PMEMoid val = btree_map_get(pop, D_RO(root)->tree, i);
        uint64_t *ptr = pmemobj_direct(val);
        assert(OID_INSTANCEOF(val, uint64_t) && ptr && *ptr == i);
        printf("\t%lu => %lu!\n", i, *ptr);
    }

    pmemobj_close(pop);

    return 0;
}