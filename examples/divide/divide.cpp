/*
 * First KLEE tutorial: testing a small function
 */

#include "klee/klee.h"
#include <immintrin.h>
#include <limits>

int normal_divide(int numerator, int denominator) {
	return numerator / denominator;
}

int main() {
  int a, b;
  klee_make_symbolic(&a, sizeof(a), "numerator");
  klee_make_symbolic(&b, sizeof(b), "denominator");
  return normal_divide(a, b);
} 
