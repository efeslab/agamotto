#include "common.h"

/**
 * Without heuristic: 3 paths (2 regular, 1 after error)
 * With heurisitc: 2 paths, 1 terminated early
 */

int mod_function(char *addr, bool mod) {
    char a = 2;
    if (mod) {
        addr[0] = a;
    } else {
        a = addr[0];
    }

    return a;
}

int main(int argc, char *argv[]) {
    char pmemaddr[BUF_LEN];

    char *real_pmem = klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    bool isMod = true;
    klee_make_symbolic(&isMod, sizeof(isMod), "choice");

    mod_function(real_pmem, isMod);

    return 0;
}
