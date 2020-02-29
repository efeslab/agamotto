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

int mod_function(char *addr, bool mod) {
    char a = 2;
    if (mod) {
        addr[0] = a;
    } else {
        a = addr[0];
    }

    return a;
}

int loop_function(char *addr, int count) {
    for (int i = 0; i < count; ++i) {
        addr[i] = (char)i;
    }

    return 0;
}

int loop_extra(char *addr, int count) {
    for (int i = 0; i < count; ++i) {
        addr[i] = (char)i;
    }

    addr[0] = 'S';

    return 0;
}

int main(int argc, char *argv[]) {
	char __attribute__((annotate("nvmptr"))) *pmemaddr;

    char data[BUF_LEN];

    pmemaddr = data;
    klee_make_symbolic(pmemaddr, BUF_LEN, "pmemaddr");

    bool control;
    int count;

    klee_make_symbolic(&control, sizeof(control), "control_var");
    klee_make_symbolic(&count, sizeof(count), "count");
    klee_assume(count >= 0 && count <= 100);

    mod_function(pmemaddr, control);
    mod_function(pmemaddr, control);

    loop_function(pmemaddr, count);
    loop_extra(pmemaddr, count);

	return 0;
}
