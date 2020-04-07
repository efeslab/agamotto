#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>

#include "klee/klee.h"

#define BUF_LEN 16384

int main() {

  char pmemaddr[BUF_LEN];
  klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

  // flushing a non-dirty line
  _mm_clflush(&pmemaddr[0]);
  _mm_sfence();

  return 0;
  
}
