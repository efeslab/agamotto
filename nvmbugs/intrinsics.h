#ifndef INTRINSICS_H
#define INTRINSICS_H

// This is how libpmem in PMDK defines the intrinsics
#define	_mm_clflushopt(addr)\
	asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)addr));
#define	_mm_clwb(addr)\
	asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)addr));

#endif // INTRINSICS_H
