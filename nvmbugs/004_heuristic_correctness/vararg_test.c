#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "klee/klee.h"

#if 1
int read_all(size_t n, ...) {
    va_list va;
    va_start(va, n);

    int ret = 0;

    for (size_t i = 0; i < n; ++i) {
        char *arg = va_arg(va, char*);
        ret += (int)arg[0];
    }

    va_end(va);

    return ret;
}   

int write_all(size_t n, ...) {
    va_list va;
    va_start(va, n);

    for (size_t i = 0; i < n; ++i) {
        char *arg = va_arg(va, char*);
        arg[0] = 'A' + (char)i;
    }

    va_end(va);

    return (int)n;
}
#else
int read_all(size_t n, char *arg) {
    int ret = 0;
    ret += (int)arg[0];
    return ret;
}   

int write_all(size_t unused, size_t n, char *arg) {
    arg[0] = 'A';
    return (int)n;
}
#endif

int main(int argc, const char *argv[]) {
    bool write_mode;
    bool use_nvm;
    char nvbuf[1024];
    char vbuf[1024];

    klee_make_symbolic(&use_nvm, sizeof(use_nvm), "nvm_switch");
    klee_make_symbolic(&write_mode, sizeof(write_mode), "read_write_switch");
    char *nvaddr = klee_pmem_mark_persistent(nvbuf, 1024, "nvbuf");
    char *vaddr = vbuf;
    char *buffer = vaddr;

    klee_assume(use_nvm == true);
    // klee_assume(write_mode == false);

    if (use_nvm) buffer = nvaddr;

    buffer[0] = 'A';
    klee_assume(buffer[0] == 'A');

#if 1
    if (write_mode) {
        return write_all(1, buffer);
    } else {
        return read_all(1, buffer);
    }
#else 
    if (write_mode) {
        (void)write_all(0, 1, buffer);
    } else {
        (void)read_all(1, buffer);
    }

    return 0;
#endif
}