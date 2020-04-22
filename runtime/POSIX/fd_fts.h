//===-- fd_fts.h -----------------------------------------------*- C++ -*--===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_FD_FTS_H
#define KLEE_FD_FTS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

FTS *fts_open(char * const *path_argv, int options,
              int (*compar)(const FTSENT **, const FTSENT **));

FTSENT *fts_read(FTS *ftsp);

FTSENT *fts_children(FTS *ftsp, int instr);

int fts_set(FTS *ftsp, FTSENT *f, int instr);

int fts_close(FTS *ftsp);

#endif // KLEE_FS_FTS_H