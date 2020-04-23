/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#ifndef CLOUD9_POSIX_SYMFS_H_
#define CLOUD9_POSIX_SYMFS_H_

#include <sys/types.h>

#include "config.h"
#include "buffers.h"

struct disk_file;

typedef struct {
  int (*truncate)(struct disk_file *dfile, size_t size);
  ssize_t (*read)(struct disk_file *dfile, void *buf, size_t count, off_t offset);
  ssize_t (*write)(struct disk_file *dfile, const void *buf, size_t count, off_t offset);
} disk_file_ops_t;

// model a symbolic regular file
// NOTE: we do not model the hierarchical structure of in the symbolic
// filesystem. Thus the name of each regular file is just a single name, not a
// path. And any file open requests ending with this name will be considered as
// symbolic.
typedef struct disk_file {
  unsigned int size; /* in bytes */
  struct stat64 *stat;
  char *name; /* not including directory structure */
  disk_file_ops_t ops;
  // file contents is managed by a block_buffer
  block_buffer_t bbuf;
} disk_file_t;  // The "disk" storage of the file


enum sym_file_type {
  // both file content and file stats are symbolic
  // file name is A...Z
  PURE_SYMBOLIC = 0,
  // file content is symbolic and file stats is set to a given concrete file
  // file name is the same as the given file
  SYMBOLIC = 2,
  // both file content and file stats are concrete
  // file name is the same as the given file
  CONCRETE = 3
};

typedef struct {
  unsigned n_sym_files; /* number of symbolic input files, excluding stdin */
  disk_file_t *sym_stdin, *sym_stdout;
  unsigned stdout_writes; /* how many chars were written to stdout */
  disk_file_t *sym_files; /* this is statically allocated */

  /* --- */
  /* the maximum number of failures on one path; gets decremented after each failure */
  unsigned max_failures; 

  /* Which read, write etc. call should fail */
  int *read_fail, *write_fail, *close_fail, *ftruncate_fail, *getcwd_fail;
  int *chmod_fail, *fchmod_fail;

  // allow non-RD_ONLY (unsafe) access to concrete files
  char allow_unsafe;
  // keep per-state concrete file offsets. Enable this flag is cloud9's default
  // option as well as klee's previous behaviour.
  char overlapped_writes;
} filesystem_t;

typedef struct {
  union {
    // file_size denotes the size of a pure symbolic file
    int file_size;
    // file_path denotes the backend of a symbolic/concrete file
    // When creating a SYMBOLIC file, only the basename of this path will be
    // used as the file name.
    const char *file_path;
  };
  enum sym_file_type file_type;
} sym_file_descriptor_t;

typedef struct {
  // number of symbolic files, excluding stdin, stdout.
  unsigned n_sym_files;
  // 0 if stdin is treated concretely
  // otherwise stdin is symbolic and this field denotes the length
  unsigned sym_stdin_len;
  sym_file_descriptor_t sym_files[MAX_FILES];
  // see the corresponding descriptions in file_system_t
  char allow_unsafe;
  char overlapped_writes;
  // 0 if stdin is a character file, 1 if stdin is a regualr file (redirected
  // from a file)
  char sym_file_stdin_flag;
  // 1 if stdout should be symbolic, 0 otherwise
  char sym_stdout_flag;
  // max_failures: maximum number of system call failures
  unsigned max_failures;
} fs_init_descriptor_t;

extern filesystem_t __sym_fs;

disk_file_t *__get_sym_file(const char *pathname);

void klee_init_symfs(fs_init_descriptor_t *fid);

#endif  //CLOUD9_POSIX_SYMFS_H_
