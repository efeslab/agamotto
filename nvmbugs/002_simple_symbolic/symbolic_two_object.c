#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include <immintrin.h>

#include <klee/klee.h>

/**
 * We expect to see two errors.
 */

int main() {
  int fd = open("PMEM", O_RDWR | O_CREAT | O_TRUNC, 0600);
  int size = getpagesize() * 8;
  char *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  printf("mmap done\n");
  if (addr == MAP_FAILED){
    return 1;
  }

  assert(klee_pmem_is_pmem(addr, size) && "mmap-ed a non-pmem-backed file!");

  int a = 0, b = getpagesize() * 4;

  // We should see two errors on two different MemoryObjects
  addr[a] = 'a';
  addr[b] = 'b';
  
  // munmap(addr, size);
  return 0;
}