//===-- klee_div_zero_check.c ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/klee.h"

void klee_div_one_check(long long z) {
  if (z == 1)
    klee_report_error(__FILE__, __LINE__, "divide by one", "div1.err");
}
