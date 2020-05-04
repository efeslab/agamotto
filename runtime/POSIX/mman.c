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
#include <stdint.h>
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
#include "symfs.h"
#include "files.h"
#include "config.h"

////////////////////////////////////////////////////////////////////////////////
// helper functions
////////////////////////////////////////////////////////////////////////////////

typedef struct {
  void *start;
  void *end;
  disk_file_t *df;
} mmap_entry_t;

static mmap_entry_t mmap_entries[MAX_FDS];
static int next_free = 0;

static int find_index(void *start, void *end) {
  int i;
  /**
   * Just do a linear search. We start from the back so we get the most recent
   * entry.
   * 
   * Is this particularly efficient?
   * Not really. But I'm relying on the following facts:
   * 1. This happens like a couple of times per program.  
   * 2. I don't want to build a different data structure or sort the list.
   */
  for (i = next_free - 1; i >= 0; --i) {
    mmap_entry_t *e = mmap_entries + i;
    if (e->start <= start && e->end >= end) {
      return i;
    }
  }

  return -1;
}

static disk_file_t *get_mmap_file(int index) {
  if (index < 0 || index >= next_free) {
    klee_error("bad mmap_entries index!");
  }

  return mmap_entries[index].df;
}

static void insert_entry(disk_file_t *df, void *start, void *end) {
  if (next_free >= MAX_FDS) {
    klee_error("too many mmaps!");
  }

  if (find_index(start, end) > 0) return;

  mmap_entries[next_free].start = start;
  mmap_entries[next_free].end = end;
  mmap_entries[next_free].df = df;
  
  ++next_free;
}

static void remove_entry(int index) {
  int i;

  if (index + 1 < next_free) {
    /**
     * Move all the existing entries down one. 
     * 
     * Is this particularly efficient?
     * Not really. But I'm relying on the following facts:
     * 1. This happens like a couple of times per program.  
     * 2. I don't want to build a different data structure or sort the list.
     */
    for (i = index; i + 1 < next_free; ++i) {
      mmap_entries[i] = mmap_entries[i+1];
    }
  }

  --next_free; 
}


////////////////////////////////////////////////////////////////////////////////
// mmap functions
////////////////////////////////////////////////////////////////////////////////

void *mmap_sym(disk_file_t* df, size_t length, off_t offset) {

  if (!df || df->pmem_type == NOT_PMEM) {
    klee_error("mmap only supports symbolic files that are persistent files");
    return MAP_FAILED;
  }

  if (!df->bbuf.contents || !df->size) {
    klee_error("pmem file not opened prior to mapping");
    return MAP_FAILED;
  }

  size_t pgsz = getpagesize();

  if (offset % pgsz != 0) {
    klee_error("mmap invoked without a page-aligned offset");
    return MAP_FAILED;
  }

  size_t actual_length = (length % pgsz == 0) ? length : (length + pgsz) - (length % pgsz);
  if ((offset + actual_length) > df->size) {
    klee_error("trying to map beyond the file size!");
    return MAP_FAILED;
  }

  // finally, good to actual perform the mapping
  size_t page_start = offset / pgsz;
  size_t page_end = page_start + (actual_length / pgsz);
  // want to increment page_refs in interval: [page_start, page_end)
  for (; page_start < page_end; page_start++) {
    assert(klee_pmem_is_pmem(df->bbuf.contents + (pgsz * page_start), pgsz));
    df->bbuf.page_refs[page_start]++;
  }

  return (void*) (df->bbuf.contents + offset);
}

void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset) __attribute__((weak));
void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset) {
  int actual_fd = fd;
  size_t actual_size = __concretize_size(length);

  if (!(flags & MAP_ANONYMOUS)) {
    fd_entry_t *fde = __get_fd(fd);
    if (!fde) {
      errno = EBADF;
      return MAP_FAILED;
    }

    if (fde->attr & eIsFile) {
      file_t *file_object = (file_t *)fde->io_object;
      if (file_object->storage) {
        void *start = mmap_sym(file_object->storage, actual_size, offset);
        void *end = start + actual_size;
        insert_entry(file_object->storage, start, end);
        return start;
      }

      klee_warning("Using poorly-supported real-file MMAP!");
      actual_fd = file_object->concrete_fd; 
    } else {
      klee_error("Cannot mmap non-files!");
    }
  }

  void* ret = (void*)syscall(__NR_mmap, start, actual_size, prot, flags, actual_fd, offset);
  posix_debug_msg("real mmap path! (start=%p, length=%lu/%lu, prot=%d, "
                      "flags=%d, fd=%d, offset=%ld) => %p (%lu)\n",
                  start, length, actual_size, prot, flags, fd, offset, ret, 
                  (unsigned long)ret);

  if (ret != MAP_FAILED) {
    // Do this in page sizes to make unmap easier
    size_t pgsz = (size_t)getpagesize();
    void *addr;
    for (addr = ret; addr < ret + actual_size; addr += pgsz) {
      klee_define_fixed_object(addr, pgsz);
    }
  }

  return ret;
}

void *mmap64(void *start, size_t length, int prot, int flags, int fd, off64_t offset) __attribute__((weak));
void *mmap64(void *start, size_t length, int prot, int flags, int fd, off64_t offset) {
  klee_warning_once("iangneal: implementing mmap64 as mmap");
  return mmap(start, length, prot, flags, fd, offset);
}

int munmap_sym(char* start, size_t length, disk_file_t* df, int entry_index) {
  // check for complete enclosure
  if (!(df->bbuf.contents <= start && start+length <= df->bbuf.contents + df->size)) {
    klee_error("munmap invoked on [start, start+length) that's not fully included in pmem file");
    return -1;
  }

  size_t pgsz = getpagesize();
  unsigned offset = start - df->bbuf.contents;
  if (offset % pgsz || length % pgsz) {
    klee_warning("arguments passed to munmap are not page aligned; will round to enclosing pages");
  }

  unsigned page_start = offset / pgsz;
  unsigned page_end = page_start + ceil(length / (double)pgsz);
  bool all_freed = true;
  // decrement page_refs in interval [page_start, page_end)
  // if ref count goes to zero, check that the page is persisted
  for (; page_start < page_end; page_start++) {
    if (df->bbuf.page_refs[page_start] == 0) {
      klee_error("munmap invoked on page with ref count already equal to 0");
      return -1;
    }

    df->bbuf.page_refs[page_start]--;
    if (df->bbuf.page_refs[page_start] == 0) {
      // Force a persistent check on unmap to ensure we check. We can check
      // on sfences, but if a program also omits those, this will be our only
      // check.
      if (!klee_pmem_is_pmem(df->bbuf.contents + (pgsz*page_start), pgsz)) {
        klee_error("Symbolically unmapping non-pmem!");
      }

      klee_pmem_check_persisted(df->bbuf.contents + (pgsz*page_start), pgsz);
    } else {
      all_freed = false;
    }
  }

  // Now we see if we should remove the mapping
  if (all_freed) {
    remove_entry(entry_index);
  }

  return 0;
}

int munmap(void *start, size_t length) __attribute__((weak));
int munmap(void *start, size_t length) {
  size_t actual_size = __concretize_size(length);
  void *end = start + actual_size;
  int entry_index = find_index(start, end);
  if (entry_index >= 0) {
    disk_file_t *df = get_mmap_file(entry_index);
    // call munmap_pmem if the following intervals overlap:
    // [df->contents, df->contents+df->size) and [start, start+length)
    if (df->bbuf.contents < (char*)end && (char*)start < df->bbuf.contents + df->size) {
      return munmap_sym(start, length, df, entry_index);
    }
  }

  posix_debug_msg("munmap(start=%p, length=%lu)\n", start, actual_size);

  size_t pgsz = (size_t)getpagesize();
  start = __concretize_ptr(start);
  // FIXME be able to partially unmap objects (to please jemalloc)
  size_t pgoffset = (uintptr_t)start % pgsz;
  if (pgoffset != 0) {
    // Start on the next full page
    start += (pgsz - pgoffset);
  }
  void *addr;
  for (addr = start; addr < start + actual_size; addr += pgsz) {
    // snprintf(msg, 4096, "\tundef(addr=%p, length=%lu)", addr, pgsz);
    // klee_warning(msg);

    klee_undefine_fixed_object(addr);
  }

  posix_debug_msg("munmap done.\n");

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

int mprotect(void *addr, size_t len, int prot) __attribute__((weak));
int mprotect(void *addr, size_t len, int prot) {
  klee_warning("treating mprotect as a no-op (SUCCESS)");
  return 0;
}
