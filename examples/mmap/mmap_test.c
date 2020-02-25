#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

#include <klee/klee.h>

int main(int argc, const char *argv[]) {
    size_t sz = 4096;
    void *addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    klee_assert(errno == 0);
    klee_assert(addr);
    char *thing = (char*)addr;
    thing[0] = 1;
    klee_assert(thing[0]);
    munmap(addr, sz);
    return 0;
}
