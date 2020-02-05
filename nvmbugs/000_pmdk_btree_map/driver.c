#include "btree_map.h"

#define LAYOUT_NAME "000_btree_map_test_driver"

int main(int argc, const char *argv[]) {
    PMEMobjpool *pop = pmemobj_create(argv[1], LAYOUT_NAME, PMEMOBJ_MIN_POOL, 0666);

    pmemobj_close(pop);

    return 0;
}