#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <stdbool.h>

#include <klee/klee.h>

#define BUF_LEN 4096

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

    klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    bool isMod = true;
    klee_make_symbolic(&isMod, sizeof(isMod), "choice");

    mod_function(pmemaddr, isMod);

    klee_pmem_check_persisted(pmemaddr, BUF_LEN);

    return 0;
}
