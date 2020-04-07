#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>

#include <klee/klee.h>

/* copying 4k at a time to pmem for this example */
#define BUF_LEN 4096*4

int mod_function(char *addr) {
    addr[0] = 2;
    return 0;
}

int no_mod_function(char *addr) {
    char x = *addr;
    return 0;
}

int
main(int argc, char *argv[])
{
    char pmemaddr[BUF_LEN];

    klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

    mod_function(pmemaddr);
    no_mod_function(pmemaddr);

    klee_pmem_check_persisted(pmemaddr, BUF_LEN);

	return 0;
}
