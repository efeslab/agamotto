#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>

#include "klee/klee.h"

int main() {
  int fd = open("data", O_RDWR | O_CREAT | O_TRUNC, 0600);
  int size = 128;
  char *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == (void*) -1 ){
    exit(1);
  }

  int a, b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_assume(0 <= a & a < size);
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_assume(0 <= b & b < size);

  // klee_assume(a == 0);
  // klee_assume(b == );

  //will only succeed if a and b on same cache-line
  addr[a] = 'a';
  _mm_clflush(&addr[b]);
  _mm_sfence();

  klee_pmem_check_persisted(&addr[a], sizeof(addr[a]));
  
  munmap(addr, size);
  printf("Program done\n");
  return 0;
  
}