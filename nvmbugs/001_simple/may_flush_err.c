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

  int a;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_assume(0 <= a & a < BUF_LEN);
  pmemaddr[a] = 25;

  // CacheLine 0 might be clean, or might be dirty!
  // if this results in error, then check persist must also fail
  // since 'a' cannot be on the smae cache line as 0, which got flushed
  _mm_clflush(&pmemaddr[0]);

  _mm_sfence();

  // might persist, might not!
  klee_pmem_check_persisted(&pmemaddr[a], sizeof(pmemaddr[a]));
  
  return 0;
  
}
