#include "common.h"

/**
 */

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
