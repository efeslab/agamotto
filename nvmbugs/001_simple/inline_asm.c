/**
 * Making sure KLEE can raise all this inline asm to
 * the proper intrinsics.
 */

#define _mm_mfence()\
	asm volatile("mfence" ::: "memory")
#define _mm_sfence()\
	asm volatile("sfence" ::: "memory")
#define	_mm_clflushopt(addr)\
	asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)addr));
#define	_mm_clwb(addr)\
	asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)addr));

int main() {
	char stackvar;

	_mm_sfence();
	_mm_mfence();
	_mm_clflushopt(&stackvar);
	_mm_clwb(&stackvar);

	return 0;
}
