#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>

/* copying 4k at a time to pmem for this example */
#define BUF_LEN 4096

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
	char __attribute__((annotate("nvmptr"))) *pmemaddr;

    char data[BUF_LEN];

    pmemaddr = data;

    mod_function(pmemaddr);
    no_mod_function(pmemaddr);

	return 0;
}
