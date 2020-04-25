#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <assert.h>
#include <immintrin.h>

#include <klee/klee.h>

int main() {
	int size = 128;
	char addr[size];
	klee_pmem_mark_persistent(&addr, sizeof(addr), "addr");

	int a, b;
	klee_make_symbolic(&a, sizeof(a), "a");
	klee_assume(0 <= a & a < size);
	klee_make_symbolic(&b, sizeof(b), "b");
	klee_assume(0 <= b & b < size);

	//will only succeed if a and b on same cache-line
	addr[a] = 'a';
	_mm_clwb(&addr[b]);
	_mm_sfence();

	klee_pmem_check_persisted(&addr[a], sizeof(addr[a]));

	return 0;
}
