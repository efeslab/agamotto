#include "common.h"

/**
 */

int main(int argc, char *argv[]) {
    char pmemaddr[BUF_LEN];
    char voladdr[BUF_LEN];

    char *real_pmem = klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    bool isNvm = true;
    klee_make_symbolic(&isNvm, sizeof(isNvm), "choice");

    for (int i = 0; i < BUF_LEN; ++i) {
        if (isNvm) {
            real_pmem[i] = 'N';
        } else {
            voladdr[i] = 'V';
        }
    }

    return 0;
}
