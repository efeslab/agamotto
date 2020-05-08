#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <assert.h>
#include <immintrin.h>

#include <klee/klee.h>

void check(bool init_zero) {
  static int id = 0;
  char buf[128];
  snprintf(buf, 128, "pmem_%d", id++);
  char *pmem = (char*)klee_pmem_alloc_pmem(4096, buf, true, NULL);
  assert(klee_pmem_is_pmem(pmem, 4096) && "mmap-ed a non-pmem-backed file!");

  char a;
  klee_make_symbolic(&a, sizeof(a), "a");

  pmem[0] = a;
  _mm_clflush(pmem);
  // will fail if we don't get a symbolic back.
  if (pmem[0] != a) {
    fprintf(stderr, "pmem[0] != a\n");
    exit(-1);
  }

  // should fork
  if (pmem[0]) {
    printf("high!\n");
  } else {
    printf("low!\n");
  }
}

int main() {
  check(true);
  check(false);
  return 0;
}