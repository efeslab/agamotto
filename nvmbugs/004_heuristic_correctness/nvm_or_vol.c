#include "common.h"

/**
 * Without heuristic: 3 paths (2 regular, 1 after error) 
 * -- 54 inst
 * With heurisitc: 2 paths, 1 terminated early
 * -- 47 inst
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
    char voladdr[BUF_LEN];

    char *real_pmem = klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    bool isNvm = true;
    klee_make_symbolic(&isNvm, sizeof(isNvm), "choice");

    if (isNvm) {
        real_pmem[0] = 'N';
    } else {
        voladdr[0] = 'V';
    }

    return 0;
}
