#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>

#include <emmintrin.h>	// _mm_stream_si128
#include <xmmintrin.h>	// _mm_sfence

#include <klee/klee.h>

/* copying 4k at a time to pmem for this example */
#define BUF_LEN 4096*4

void nontemporal_store(char *addr, void *val) {
	__m128i _val = *(__m128i*)val;
	_mm_stream_si128((__m128i *)addr, _val);
}

int
main(int argc, char *argv[])
{
	char pmemaddr[BUF_LEN];

	klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

	int number[4] = { 42, 482, 21, 68 };
	nontemporal_store(&pmemaddr[0], &number);
	_mm_sfence();

	klee_pmem_check_persisted(pmemaddr, BUF_LEN);

	return 0;
}
