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

int main(int argc, char *argv[]) {
	char __attribute__((annotate("nvmptr"))) *pmemaddr;

    char data[BUF_LEN];
    char other[BUF_LEN];

    pmemaddr = data;
    klee_make_symbolic(pmemaddr, BUF_LEN, "pmemaddr");
    klee_make_symbolic(other, BUF_LEN, "dram_buffer");

    int count;
    bool direction;

    klee_make_symbolic(&count, sizeof(count), "count");
    klee_make_symbolic(&direction, sizeof(direction), "direction");
    klee_assume(count >= 1 && count <= 100);

    for (int i = 0; i < count; ++i) {
        if (direction) pmemaddr[i] = other[i];
        else other[i] = pmemaddr[i];
    }

	return 0;
}
