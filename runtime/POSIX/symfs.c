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

#include "symfs.h"
#include "fd.h"

#include <klee/klee.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#include "common.h"
#include "buffers.h"
//#include "models.h"

filesystem_t __sym_fs;

////////////////////////////////////////////////////////////////////////////////
// Symbolic Files Operations
////////////////////////////////////////////////////////////////////////////////

static ssize_t _read_symbolic(struct disk_file *dfile, void *buf, size_t count,
                              off_t offset) {
  block_buffer_t *buff = &dfile->bbuf;
  return _block_read(buff, buf, count, offset);
}

static ssize_t _write_symbolic(struct disk_file *dfile, const void *buf,
                               size_t count, off_t offset) {
  block_buffer_t *buff = &dfile->bbuf;
  ssize_t result = _block_write(buff, buf, count, offset);
  dfile->stat->st_size = buff->size;
  return result;
}

static int _truncate_symbolic(struct disk_file *dfile, size_t size) {
  block_buffer_t *buff = &dfile->bbuf;

  if (size > buff->max_size)
    return -1;

  buff->size = size;
  dfile->stat->st_size = size;

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// FS Routines
////////////////////////////////////////////////////////////////////////////////

/* Returns pointer to the symbolic file structure if the pathname is symbolic */
disk_file_t *__get_sym_file(const char *pathname) {
  if (!pathname)
    return NULL;
  if (pathname[0] == 0)
    return NULL;

  unsigned i;
  for (i = 0; i < __sym_fs.n_sym_files; ++i) {
    // if the basename of a request path is matched with a known symbolic file,
    // then we return that symbolic file.
    // NOTE: no symbolic hierarchical directory structure modeled here.
    char matched = 0;
    const char *basename = strrchr(pathname, '/');
    const char *sym_name = __sym_fs.sym_files[i].name;
    if (basename) {
      matched = (strcmp(basename+1, sym_name) == 0);
    } else {
      matched = (strcmp(pathname, sym_name) == 0);
    }
    if (matched) {
      printf("get symbolic file %s\n", __sym_fs.sym_files[i].name);
      disk_file_t *df = &__sym_fs.sym_files[i];
      if (df->stat->st_ino == 0)
        return NULL;
      return df;
    }
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// FS Initialization
////////////////////////////////////////////////////////////////////////////////

/* unused cloud 9 function
static int __isupper(const char c) {
  return (('A' <= c) & (c <= 'Z'));
}
*/
static void _fill_stats_field(disk_file_t *dfile, const struct stat64 *defstats) {
  struct stat64 *stat = dfile->stat;

  /* For broken tests */
  if (!klee_is_symbolic(stat->st_ino) && 
      (stat->st_ino & 0x7FFFFFFF) == 0)
    stat->st_ino = defstats->st_ino;

  /* Important since we copy this out through getdents, and readdir
     will otherwise skip this entry. For same reason need to make sure
     it fits in low bits. */
  klee_assume((stat->st_ino & 0x7FFFFFFF) != 0);

  /* uclibc opendir uses this as its buffer size, try to keep
     reasonable. */
  klee_assume((stat->st_blksize & ~0xFFFF) == 0);

  klee_prefer_cex(stat, !(stat->st_mode & ~(S_IFMT | 0777)));
  klee_prefer_cex(stat, stat->st_dev == defstats->st_dev);
  klee_prefer_cex(stat, stat->st_rdev == defstats->st_rdev);
  klee_prefer_cex(stat, (stat->st_mode&0700) == 0600);
  klee_prefer_cex(stat, (stat->st_mode&0070) == 0040);
  klee_prefer_cex(stat, (stat->st_mode&0007) == 0004);
  klee_prefer_cex(stat, (stat->st_mode&S_IFMT) == S_IFREG);
  klee_prefer_cex(stat, stat->st_nlink == 1);
  klee_prefer_cex(stat, stat->st_uid == defstats->st_uid);
  klee_prefer_cex(stat, stat->st_gid == defstats->st_gid);
  klee_prefer_cex(stat, stat->st_blksize == 4096);
  klee_prefer_cex(stat, stat->st_atime == defstats->st_atime);
  klee_prefer_cex(stat, stat->st_mtime == defstats->st_mtime);
  klee_prefer_cex(stat, stat->st_ctime == defstats->st_ctime);
  stat->st_size = 0;
  stat->st_blocks = 8;
}

static void _init_stats(disk_file_t *dfile, const char *symname,
    const struct stat64 *defstats, int symstats) {
  static char namebuf[64];

  dfile->stat = (struct stat64*)malloc(sizeof(struct stat64));

  if (symstats) {
    strcpy(namebuf, symname);
    strcat(namebuf, "-stat");
    klee_make_symbolic(dfile->stat, sizeof(struct stat64), namebuf);
    klee_make_shared(dfile->stat, sizeof(struct stat64));
    _fill_stats_field(dfile, defstats);
  } else {
    memcpy(dfile->stat, defstats, sizeof(struct stat64));
  }
}

static void _init_file_name(disk_file_t *dfile, const char *symname) {
  size_t namelen = strlen(symname);
  assert(namelen < MAX_PATH_LEN);
  dfile->name = (char*)malloc(MAX_PATH_LEN);
  memset(dfile->name, 0, MAX_PATH_LEN);
  strncpy(dfile->name, symname, MAX_PATH_LEN);
}

static size_t _read_file_contents(const char *file_path, size_t size, char *orig_contents) {
  int orig_fd = CALL_UNDERLYING(open, file_path, O_RDONLY);
  assert(orig_fd >= 0 && "Could not open original file.");

  size_t current_size = 0;
  ssize_t bytes_read = 0;
  while ((bytes_read = CALL_UNDERLYING(
      read, orig_fd, orig_contents + current_size, size - current_size))) {
    if (bytes_read < 0) {
      klee_warning("Error while reading original file.");
      break;
    }
    current_size += bytes_read;
    if (current_size == size) {
      break;
    }
  }

  CALL_UNDERLYING(close, orig_fd);

  return current_size;
}

static void _init_pure_symbolic_buffer(disk_file_t *dfile, size_t maxsize,
    const char *symname) {
  static char namebuf[64];

  // Initializing the buffer contents...
  block_buffer_t *buff = &dfile->bbuf;
  _block_init(buff, maxsize);
  buff->size = maxsize;

  strcpy(namebuf, symname);
  klee_make_symbolic(buff->contents, maxsize, namebuf);
  klee_make_shared(buff->contents, maxsize);
}

static void _init_dual_buffer(disk_file_t *dfile, const char *origpath,
    size_t size, const char *symname, int make_symbolic) {

  block_buffer_t *buff = &dfile->bbuf;
  _block_init(buff, size);
  buff->size = size;

  if (make_symbolic) {
    static char namebuf[64];
    strcpy(namebuf, symname);

    klee_make_symbolic(buff->contents, size, namebuf);
    klee_make_shared(buff->contents, size);
  } else {
    _read_file_contents(origpath, size, buff->contents);
  }
}

// NOTE: the SYMBOLIC file has the same file name as the given file (origname)
static disk_file_t *_create_dual_file(disk_file_t *dfile, const char *origpath,
    int make_symbolic) {
  struct stat64 s;
  int res = CALL_UNDERLYING(stat, origpath, &s);
  assert(res == 0 && "Could not get the stat of the original file.");
  const char *basename = strrchr(origpath, '/');
  const char *symname;
  if (basename) {
    symname = basename;
  } else {
    symname = origpath;
  }

  _init_file_name(dfile, symname);
  _init_dual_buffer(dfile, origpath, s.st_size, symname, make_symbolic);

  dfile->stat = (struct stat64*)malloc(sizeof(struct stat64));
  memcpy(dfile->stat, &s, sizeof(struct stat64));

  // Register the operations
  memset(&dfile->ops, 0, sizeof(dfile->ops));
  dfile->ops.read = _read_symbolic;
  dfile->ops.write = _write_symbolic;
  dfile->ops.truncate = _truncate_symbolic;

  return dfile;
}

static disk_file_t *_create_pure_symbolic_file(disk_file_t *dfile,
                                               size_t maxsize,
                                               const char *symname,
                                               const struct stat64 *defstats,
                                               int symstats) {
  _init_file_name(dfile, symname);
  _init_pure_symbolic_buffer(dfile, maxsize, symname);
  _init_stats(dfile, symname, defstats, symstats);

  // Update the stat size
  block_buffer_t *buff = &dfile->bbuf;
  dfile->stat->st_size = buff->size;

  // Register the operations
  memset(&dfile->ops, 0, sizeof(dfile->ops));
  dfile->ops.read = _read_symbolic;
  dfile->ops.write = _write_symbolic;
  dfile->ops.truncate = _truncate_symbolic;

  return dfile;
}

void klee_init_symfs(fs_init_descriptor_t *fid) {
  struct stat64 def_stat;

  int res = stat64(".", &def_stat);
  assert(res == 0 && "Could not get default stat64 values");

  memset(&__sym_fs, 0, sizeof(__sym_fs));
  klee_make_shared(&__sym_fs, sizeof(__sym_fs));
  unsigned n_sym_files = fid->n_sym_files;
  __sym_fs.n_sym_files = n_sym_files;
  __sym_fs.sym_files = malloc(sizeof(*__sym_fs.sym_files) * n_sym_files);
  klee_make_shared(__sym_fs.sym_files,
                   sizeof(*__sym_fs.sym_files) * n_sym_files);

  unsigned pure_symbolic_cnt = 0;
  char pure_symbolic_name[] = "?";
  int i;
  for (i=0; i < n_sym_files; ++i) {
    disk_file_t *dfile = &__sym_fs.sym_files[i];
    switch (fid->sym_files[i].file_type) {
      case PURE_SYMBOLIC:
        pure_symbolic_name[0] = 'A' + pure_symbolic_cnt;
        ++pure_symbolic_cnt;
        assert(pure_symbolic_cnt <= MAX_PURE_SYM_FILES);
        _create_pure_symbolic_file(
            dfile, fid->sym_files[i].file_size,
            pure_symbolic_name, &def_stat, /*make stats symbolic?*/ 1);
        break;
      case SYMBOLIC:
        _create_dual_file(dfile, fid->sym_files[i].file_path,
                          /*make stats symbolic?*/ 1);
        break;
      case CONCRETE:
        _create_dual_file(dfile, fid->sym_files[i].file_path,
                          /*make stats symbolic?*/ 0);
        break;
    }
  }

  /* setting symbolic stdin */
  if (fid->sym_stdin_len) {
    __sym_fs.sym_stdin = malloc(sizeof(*__sym_fs.sym_stdin));
    _create_pure_symbolic_file(__sym_fs.sym_stdin, fid->sym_stdin_len, "stdin", &def_stat, 1);
    __exe_env.fds[0].io_object = (file_base_t*)__sym_fs.sym_stdin;
    if (fid->sym_file_stdin_flag) {
      klee_assume(!S_ISCHR(__sym_fs.sym_stdin->stat->st_mode));
    }
    else {
      klee_assume(S_ISCHR(__sym_fs.sym_stdin->stat->st_mode));
    }
  } else __sym_fs.sym_stdin = NULL;

  __sym_fs.max_failures = fid->max_failures;
  if (__sym_fs.max_failures) {
    __sym_fs.read_fail = malloc(sizeof(*__sym_fs.read_fail));
    __sym_fs.write_fail = malloc(sizeof(*__sym_fs.write_fail));
    __sym_fs.close_fail = malloc(sizeof(*__sym_fs.close_fail));
    __sym_fs.ftruncate_fail = malloc(sizeof(*__sym_fs.ftruncate_fail));
    __sym_fs.getcwd_fail = malloc(sizeof(*__sym_fs.getcwd_fail));

    klee_make_symbolic(__sym_fs.read_fail, sizeof(*__sym_fs.read_fail), "read_fail");
    klee_make_symbolic(__sym_fs.write_fail, sizeof(*__sym_fs.write_fail), "write_fail");
    klee_make_symbolic(__sym_fs.close_fail, sizeof(*__sym_fs.close_fail), "close_fail");
    klee_make_symbolic(__sym_fs.ftruncate_fail, sizeof(*__sym_fs.ftruncate_fail), "ftruncate_fail");
    klee_make_symbolic(__sym_fs.getcwd_fail, sizeof(*__sym_fs.getcwd_fail), "getcwd_fail");
  }

  /* setting symbolic stdout */
  if (fid->sym_stdout_flag) {
    __sym_fs.sym_stdout = malloc(sizeof(*__sym_fs.sym_stdout));
    _create_pure_symbolic_file(__sym_fs.sym_stdin, 1024, "stdout", &def_stat, 1);
    __exe_env.fds[1].io_object = (file_base_t*)__sym_fs.sym_stdout;
    klee_assume(S_ISCHR(__sym_fs.sym_stdout->stat->st_mode));
    __sym_fs.stdout_writes = 0;
  }
  else __sym_fs.sym_stdout = NULL;
}
