//===-- fd_fts.c ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "fd_fts.h"
#include "common.h"


FTS *fts_open(char * const *path_argv, int options,
              int (*compar)(const FTSENT **, const FTSENT **)) {
  int i = 0;
  while (path_argv[i])
    posix_debug_msg("%s\n", path_argv[i++]);
  return NULL;
}

FTSENT *fts_read(FTS *ftsp) {
  errno = ENOTSUP;
  return NULL;
}

FTSENT *fts_children(FTS *ftsp, int instr) {
  errno = ENOTSUP;
  return NULL;
}

int fts_set(FTS *ftsp, FTSENT *f, int instr) {
  errno = ENOTSUP;
  return -1;
}

int fts_close(FTS *ftsp) {
  errno = ENOTSUP;
  return -1;
}