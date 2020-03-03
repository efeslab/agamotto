#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <stdbool.h>

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
	char __attribute__((annotate("nvmptr"))) *pmemaddr;

    char data[BUF_LEN];

    pmemaddr = data;

    mod_function(pmemaddr, true);
    mod_function(pmemaddr, false);
    mod_function(pmemaddr, true);
    mod_function(pmemaddr, false);

	return 0;
}
