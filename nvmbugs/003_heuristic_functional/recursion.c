#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "klee/klee.h"

void rec_write(char *buf, int n) {
    if (n > 0) {
        rec_write(buf, n - 1);
    }

    buf[n] = 'Z';
}

int main(int argc, const char *argv[]) {
    int use_nvm = 0;
    char nvbuf[1024];
    char vbuf[1024];

    klee_make_symbolic(&use_nvm, sizeof(use_nvm), "nvm_switch");
    char *nvaddr = klee_pmem_mark_persistent(nvbuf, 1024, "nvbuf");

    if (use_nvm) {
        rec_write(nvaddr, 10);
    } else {
        rec_write(vbuf, 10);
    }

    return 0;
}