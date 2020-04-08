#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <assert.h>
#include <immintrin.h>

#include <klee/klee.h>

int main() {
  int fd = open("PMEM", O_RDWR | O_CREAT | O_TRUNC, 0600);
  int size = 128;
  char *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  printf("mmap done\n");
  if (addr == MAP_FAILED){
    return 1;
  }

  assert(klee_pmem_is_pmem(addr, size) && "mmap-ed a non-pmem-backed file!");

  int a, b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_assume(0 <= a & a < size);
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_assume(0 <= b & b < size);

  //will only succeed if a and b on same cache-line
  addr[a] = 'a';
  _mm_clwb(&addr[b]);
  _mm_sfence();

  // printf("check\n");
  // klee_pmem_check_persisted(&addr[a], sizeof(addr[a]));
  // printf("check done\n");
  
  // munmap(addr, size);
  // printf("Program done\n");
  return 0;
}