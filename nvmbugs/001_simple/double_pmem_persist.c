#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include <immintrin.h>

#include <klee/klee.h>
#include <libpmem.h>

/**
 * We don't expect double flush stuff
 */

int main() {
  int fd = open("PMEM", O_RDWR | O_CREAT | O_TRUNC, 0600);
  int size = getpagesize() * 4;
  char *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  printf("mmap done\n");
  if (addr == MAP_FAILED){
    return 1;
  }

  assert(klee_pmem_is_pmem(addr, size) && "mmap-ed a non-pmem-backed file!");

  addr[32] = 'A';
  pmem_persist(addr, sizeof(addr[0]));
  addr[32] = 'B';
  pmem_persist(addr, sizeof(addr[0]));
  
  munmap(addr, size);
  return 0;
}