#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "intrinsics.h"	// _mm_clwb
#include <xmmintrin.h>	// _mm_sfence

#include <klee/klee.h>

/* copying 4k at a time to pmem for this example */
#define BUF_LEN 4096*4

int
main()
{
	char pmemaddr[BUF_LEN];
	klee_pmem_mark_persistent(pmemaddr, BUF_LEN, "pmem_stack_buffer");

	pmemaddr[0] = 2;
	_mm_clwb(&pmemaddr[0]);
	_mm_sfence();

	klee_pmem_check_persisted(pmemaddr, BUF_LEN);

	return 0;
}
