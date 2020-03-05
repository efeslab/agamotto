//===-- mman.c ------------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifdef __FreeBSD__
#include "FreeBSD.h"
#endif
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#ifndef __FreeBSD__
#include <utmp.h>
#endif
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <malloc.h>
#include <sys/syscall.h>
#include <math.h>

#include "klee/klee.h"
#include "klee/Config/config.h"
#include "fd.h"

void klee_warning(const char*);
void klee_warning_once(const char*);
void klee_error(const char*);

static exe_file_t *__get_file(int fd) {
  if (fd>=0 && fd<MAX_FDS) {
    exe_file_t *f = &__exe_env.fds[fd];
    if (f->flags & eOpen)
      return f;
  }

  return 0;
}

// FIXME: better way of importing
extern exe_file_t *__get_file(int fd);

#include "klee/klee.h"

/**
 * Helpers.
 */
static void *__concretize_ptr(const void *p) {
  /* XXX 32-bit assumption */
  char *pc = (char*) klee_get_valuel((long) p);
  klee_assume(pc == p);
  return pc;
}

static size_t __concretize_size(size_t s) {
  size_t sc = klee_get_valuel((long)s);
  klee_assume(sc == s);
  return sc;
}

/**
 * Real stuff.
 */

void *mmap_sym(exe_file_t* f, size_t length, off_t offset) {
  void* err_ret = (void*) -1;
  if (!f || !__exe_fs.sym_pmem || !(f->dfile == __exe_fs.sym_pmem)) {
    klee_error("mmap only supports symbolic files that are persistent files");
    return err_ret;
  }
  exe_disk_file_t* df = f->dfile;
  if (!df || !df->contents || !df->size) {
    klee_error("pmem file not opened prior to mapping");
    return err_ret;
  }
  // FIXME: don't assume page size of 4096 in the future
  if (offset % 4096 != 0) {
    klee_error("mmap invoked without a page-aligned offset");
    return err_ret;
  }
  size_t actual_length = (length % 4096 == 0 ? length : length + 4096 - length % 4096);
  if (offset + actual_length > df->size) {
    klee_error("trying to map beyond the file size!");
    return err_ret;
  }

  // finally, good to actual perform the mapping
  size_t page_start = offset / 4096;
  size_t page_end = page_start + actual_length / 4096;
  // want to increment page_refs in interval: [page_start, page_end)
  for (; page_start < page_end; page_start++) {
    df->page_refs[page_start]++;
  }
  return (void*) (df->contents + offset);
}

void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset) __attribute__((weak));
void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset) {
  //FIXME: ref count
  char msg[4096];
  memset(msg, 0, 4096);

  int actual_fd = fd;
  size_t actual_size = __concretize_size(length);

  if (!(flags & MAP_ANONYMOUS)) {
    exe_file_t *f = __get_file(fd);
    if (!f) {
      errno = EBADF;
      return (void*)-1;
    }

    if (f->dfile) {
      return mmap_sym(f, actual_size, offset);
    }

    actual_fd = f->fd;

    // struct stat64 stat;
    // int err = __fd_fstat(fd, &stat);
    // if (err) {
    //   snprintf(msg, 4096, "mmap fstat failed! ret=%d, errno=%d (%s)", err, errno, strerror(errno));
    //   klee_error(msg);
    //   return (void*)-1;
    // }

    //actual_size = stat.st_size;
    
  }

  void* ret = (void*)syscall(__NR_mmap, start, actual_size, prot, flags, actual_fd, offset);
  snprintf(msg, 4096, "(start=%p, length=%lu/%lu, prot=%d, flags=%d, fd=%d, offset=%ld) => %p (%lu)",
           start, length, actual_size, prot, flags, fd, offset, ret, (unsigned long)ret);
  klee_warning(msg);

  if (ret != MAP_FAILED) {
    // Do this in page sizes to make unmap easier
    size_t pgsz = (size_t)getpagesize();
    for (void *addr = ret; addr < ret + actual_size; addr += pgsz) {
      klee_define_fixed_object_from_existing(addr, pgsz);
      // if (actual_fd >= 0 && !memcmp(addr, zeros, pgsz)) klee_warning("mmap-ed page is 0!");
      if (actual_fd >= 0) {
        // snprintf(msg, 4096, "pmem-%d_page-%lu (%p)", fd, ((size_t)(addr - ret)) / pgsz, addr);
        // klee_warning(msg);
        klee_pmem_mark_persistent(addr, pgsz, msg);
      }
    }
  }

  return ret;
}

void *mmap64(void *start, size_t length, int prot, int flags, int fd, off64_t offset) __attribute__((weak));
void *mmap64(void *start, size_t length, int prot, int flags, int fd, off64_t offset) {
  klee_warning_once("iangneal: implementing mmap64 as mmap");
  return mmap(start, length, prot, flags, fd, offset);
}

int munmap_sym(char* start, size_t length, exe_disk_file_t* df) {
  // check for complete enclosure
  if (!(df->contents <= start && start+length <=df->contents + df->size)) {
    klee_error("munmap invoked on [start, start+length) that's not fully included in pmem file");
    return -1;
  }
  unsigned offset = start - df->contents;
  if (offset % 4096 != 0 || length % 4096 != 0) {
    klee_warning("arguments passed to munmap are not page aligned; will round to enclosing pages");
  }
  unsigned page_start = offset / 4096;
  unsigned page_end = page_start + ceil(length / 4096.0);
  // decrement page_refs in interval [page_start, page_end)
  // if ref count goes to zero, check that the page is persisted
  for (; page_start < page_end; page_start++) {
    if (df->page_refs[page_start] == 0) {
      klee_error("munmap invoked on page with ref count already equal to 0");
      return -1;
    }
    df->page_refs[page_start]--;
    if (df->page_refs[page_start] == 0) {
      // for now, don't actually perform persistence check
      //klee_pmem_check_persisted(df->contents + 4096*page_start, 4096);
    }
  }
  return 0;
}

int munmap(void *start, size_t length) __attribute__((weak));
int munmap(void *start, size_t length) {
  size_t actual_size = __concretize_size(length);
  if (__exe_fs.sym_pmem) {
    exe_disk_file_t* df = __exe_fs.sym_pmem;
    // call munmap_pmem if the following intervals overlap:
    // [df->contents, df->contents+df->size) and [start, start+length)
    if (df->contents <= (char*)start + length && (char*)start <= df->contents + df->size) {
      return munmap_sym(start, length, df);
    }
  }

  char msg[4096];
  snprintf(msg, 4096, "munmap(start=%p, length=%lu)", start, actual_size);
  klee_warning(msg);

  size_t pgsz = (size_t)getpagesize();
  for (void *addr = start; addr < start + actual_size; addr += pgsz) {
    if (addr == start) {
      snprintf(msg, 4096, "\tundef(addr=%p, length=%lu)", addr, pgsz);
      klee_warning(msg);
    }

    klee_pmem_check_persisted(addr, pgsz);
    klee_undefine_fixed_object(addr);
  }

  klee_warning("munmap done.\n");

  return syscall(__NR_munmap, start, actual_size);
}

/**
 * Stubs.
 */

int mlock(const void *addr, size_t len) __attribute__((weak));
int mlock(const void *addr, size_t len) {
  klee_warning("ignoring (EPERM)");
  errno = EPERM;
  return -1;
}

int munlock(const void *addr, size_t len) __attribute__((weak));
int munlock(const void *addr, size_t len) {
  klee_warning("ignoring (EPERM)");
  errno = EPERM;
  return -1;
}