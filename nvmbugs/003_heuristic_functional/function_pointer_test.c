#include "common.h"

/**
 */

void do_mod(char *nvm) {
  nvm[0] = 'M';
}

int main(int argc, char *argv[]) {
    char pmemaddr[BUF_LEN];
    char voladdr[BUF_LEN];

    char *real_pmem = klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    bool isNvm = true;
    klee_make_symbolic(&isNvm, sizeof(isNvm), "choice");

    void (*fn)(char*) = &do_mod;

    if (isNvm) {
        (*fn)(real_pmem);
    } else {
        (*fn)(voladdr);
    }

    return 0;
}
