#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <stdbool.h>

#include "klee/klee.h"

#define BUF_LEN 4096

int mod_function(char *addr, bool cond1, bool cond2) {
    char a = 2;
    if (cond1) {
        if (cond2) {
            addr[0] = a;
        } else {
            a = addr[0];
        }
    } else {
        if (cond2) {
            a = addr[1];
        } else {
            a = addr[2];
        }
    }

    return a;
}

int main(int argc, char *argv[]) {
	char __attribute__((annotate("nvmptr"))) *pmemaddr;

    char data[BUF_LEN];

    pmemaddr = data;
    klee_make_symbolic(pmemaddr, BUF_LEN, "pmemaddr");

    bool cond1, cond2;
    klee_make_symbolic(&cond1, sizeof(cond1), "control_var_1");
    klee_make_symbolic(&cond2, sizeof(cond2), "control_var_2");

    mod_function(pmemaddr, cond1, cond2);

	return 0;
}
