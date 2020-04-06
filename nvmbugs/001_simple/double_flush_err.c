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

  // On same cache line, don't need to flush twice!
  pmemaddr[0] = 1;
  pmemaddr[1] = 2;
  _mm_clflush(&pmemaddr[0]);
  _mm_clflush(&pmemaddr[1]);
  _mm_sfence();

  klee_pmem_check_persisted(&pmemaddr[0], sizeof(pmemaddr[0]));
  klee_pmem_check_persisted(&pmemaddr[1], sizeof(pmemaddr[1]));
  
  return 0;
  
}
