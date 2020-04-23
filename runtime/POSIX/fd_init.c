//===-- fd_init.c ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include "fd.h"
#include "files.h"
#include "symfs.h"

#include "klee/klee.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* NOTE: It is important that these are statically initialized
   correctly, since things that run before main may hit them given the
   current way things are linked. */

/* XXX Technically these flags are initialized w.o.r. to the
   environment we are actually running in. We could patch them in
   klee_init_fds, but we still have the problem that uclibc calls
   prior to main will get the wrong data. Not such a big deal since we
   mostly care about sym case anyway. */

file_t stdin_fd_init = {
  /*__bdata*/ {1, 0, O_RDONLY},
  /* offset */ 0, /* concrete_fd */ 0, /* storage */ NULL
};
file_t stdout_fd_init = {
  /* __bdata */ {1, 0, O_WRONLY},
  /* offset */ 0, /* concrete_fd */ 1, /* storage */ NULL
};
file_t stderr_fd_init = {
  /* __bdata */ {1, 0, O_WRONLY},
  /* offset */ 0, /* concrete_fd */ 2, /* storage */ NULL
};
exe_sym_env_t __exe_env = { 
  /* fd_entry_t fds[] */
  {{ eOpen | eIsFile, (file_base_t*)&stdin_fd_init }, 
   { eOpen | eIsFile, (file_base_t*)&stdout_fd_init }, 
   { eOpen | eIsFile, (file_base_t*)&stderr_fd_init }},
  /* umask */
  022,
  /* version */
  0,
  /* save_all_writes */
  0
};

static unsigned __sym_uint32(const char *name) {
  unsigned x;
  klee_make_symbolic(&x, sizeof x, name);
  return x;
}

void klee_init_sym_env(int save_all_writes) {
  __exe_env.save_all_writes = save_all_writes;
  __exe_env.version = __sym_uint32("model_version");
  klee_assume(__exe_env.version == 1);
}
