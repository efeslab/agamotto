#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <immintrin.h>

/*  Cache line flush:
    clflush can be replaced with clflushopt or clwb, if the CPU supports clflushopt or clwb.
*/
#define asm_clflush(addr) _mm_clflush()

/*  Memory fence:
    mfence can be replaced with sfence, if the CPU supports sfence.
*/
// #define asm_mfence()                \
// ({                      \
//     __asm__ __volatile__ ("mfence":::"memory");    \
// })
#define asm_mfence() _mm_mfence()


void pflush(uint64_t *addr);

void init_pflush(int cpu_speed_mhz, int write_latency_ns);