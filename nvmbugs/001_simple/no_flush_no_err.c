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

struct volatile_byte {
  char byte;
};

int main() {

  char pmemaddr[BUF_LEN];
  klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

  struct volatile_byte *vt = (struct volatile_byte *)&pmemaddr[0];
  vt->byte = 0;

  klee_pmem_check_persisted(&pmemaddr[0], sizeof(pmemaddr[0]));
  
  return 0;
  
}
