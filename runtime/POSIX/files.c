/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
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

#include "files.h"
#include <klee/klee.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <termios.h>

#include "common.h"
//#include "models.h"
#include "symfs.h"

// __NR_lseek was removed in ubuntu 11.04
#ifdef __NR3264_lseek
#define __NR_lseek __NR3264_lseek
#endif


// fd: input fd
// fde: local declared fd_entry_t*
#define CHECK_IS_FILE(fd, fde)                                                 \
  do {                                                                         \
    fde = __get_fd(fd);                                                        \
    if (!fde) {                                                                \
      errno = EBADF;                                                           \
      return -1;                                                               \
    }                                                                          \
    if (!(fde->attr & eIsFile)) {                                              \
      errno = ESPIPE;                                                          \
      return -1;                                                               \
    }                                                                          \
  } while (0)

////////////////////////////////////////////////////////////////////////////////
// Internal File Routines
////////////////////////////////////////////////////////////////////////////////

static int __is_concrete_blocking(int concretefd, int event) {
  fd_set fds;
  _FD_ZERO(&fds);
  _FD_SET(concretefd, &fds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  int res;

  switch(event) {
  case EVENT_READ:
    res = CALL_UNDERLYING(select, concretefd+1, &fds, NULL, NULL, &timeout);
    return (res == 0);
  case EVENT_WRITE:
    res = CALL_UNDERLYING(select, concretefd+1, NULL, &fds, NULL, &timeout);
    return (res == 0);
  default:
      assert(0 && "invalid event");
  }
}

int _is_blocking_file(file_t *file, int event) {
  if (_file_is_concrete(file))
    return __is_concrete_blocking(file->concrete_fd, event);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// The POSIX API
////////////////////////////////////////////////////////////////////////////////

ssize_t _read_file(file_t *file, void *buf, size_t count, off_t offset) {
  if (_file_is_concrete(file)) {
    buf = __concretize_ptr(buf);
    count = __concretize_size(count);
    /* XXX In terms of looking for bugs we really should do this check
      before concretization, at least once the routine has been fixed
      to properly work with symbolics. */
    klee_check_memory_access(buf, count);

    int res;

    if (file->concrete_fd == 0) {
      assert(offset == -1 && "Should never read stdin with offset");
      res = CALL_UNDERLYING(read, file->concrete_fd, buf, count);
    } else if (offset >= 0)
      res = CALL_UNDERLYING(pread64, file->concrete_fd, buf, count, offset);
    else if (file->offset >= 0)
      res = CALL_UNDERLYING(pread64, file->concrete_fd, buf, count, file->offset);
    else
      res = CALL_UNDERLYING(read, file->concrete_fd, buf, count);

    if (res == -1) {
     errno = klee_get_errno();
    } else {
      // only update offset if the existing offset is valid and current fd is
      // not stdin
      if (file->offset >= 0 && offset < 0 && file->concrete_fd != 0)
        file->offset += res;
    }

    return res;
  } else if (file->storage->ops.read) {
    if (((off64_t)file->storage->size) < file->offset)
      return 0;
    ssize_t res = file->storage->ops.read(
        file->storage, buf, count, offset >= 0 ? offset : file->offset);

    if (res < 0) {
      errno = EINVAL;
      return -1;
    }

    if (offset < 0)
      file->offset += res;

    return res;
  } else {
    errno = EINVAL;
    return -1;
  }
}

ssize_t _write_file(file_t *file, const void *buf, size_t count, off64_t offset) {
  if (_file_is_concrete(file)) {
    buf = __concretize_ptr(buf);
    count = __concretize_size(count);
    offset = __concretize_offset(offset);
    /* XXX In terms of looking for bugs we really should do this check
      before concretization, at least once the routine has been fixed
      to properly work with symbolics. */
    klee_check_memory_access(buf, count);

    int res;

    posix_debug_msg("Writing concretely at (%d) %d bytes...\n",
                    file->concrete_fd, count);

    if (file->concrete_fd == 1 || file->concrete_fd == 2) {
      assert(offset == -1 && "Should never write stdout/stderr with offset");
      res = CALL_UNDERLYING(write, file->concrete_fd, buf, count);
    } else if (offset >= 0)
      res = CALL_UNDERLYING(pwrite64, file->concrete_fd, buf, count, offset);
    else if (file->offset >= 0)
      res = CALL_UNDERLYING(pwrite64, file->concrete_fd, buf, count, file->offset);
    else
      res = CALL_UNDERLYING(write, file->concrete_fd, buf, count);

    if (res == -1) {
      errno = klee_get_errno();
    } else {
      if (file->offset >= 0 && offset < 0 && file->concrete_fd != 1 &&
          file->concrete_fd != 2)
        file->offset += res;
    }

    return res;
  }

  posix_debug_msg("Writing symbolically %d bytes...\n", count);

  if (file->storage->ops.write) {
    size_t actual_count = 0;
    size_t symf_size = file->storage->size;
    if (file->offset + count <= symf_size) {
      actual_count = count;
    } else {
      if (__exe_env.save_all_writes)
        assert(0);
      else {
        if (file->offset < (off64_t)symf_size)
          actual_count = symf_size - file->offset;
      }
    }
    if (count != actual_count) {
      posix_debug_msg("write() ignores bytes: %lu->%lu\n", count, actual_count);
    }
    if (file->storage == __sym_fs.sym_stdout)
      __sym_fs.stdout_writes += actual_count;

    ssize_t res = file->storage->ops.write(
        file->storage, buf, actual_count, offset >= 0 ? offset : file->offset);

    if (res < 0) {
      errno = EINVAL;
      return -1;
    }

    if (offset < 0) {
      // not positional write
      file->offset += res;
    }

    if (res == 0)
      errno = EFBIG;

    return res;
  } else {
    errno = EINVAL;
    return -1;
  }
}

////////////////////////////////////////////////////////////////////////////////

static int _stat_dfile(disk_file_t *dfile, struct stat64 *buf) {
  if (INJECT_FAULT(fstat, ELOOP, ENOMEM)) {
    return -1;
  }

  memcpy(buf, dfile->stat, sizeof(*buf));
  return 0;
}

int _stat_file(file_t *file, struct stat64 *buf) {
  if (_file_is_concrete(file)) {
    int res = CALL_UNDERLYING(fstat, file->concrete_fd, buf);

    if (res == -1)
      errno = klee_get_errno();
    return res;
  }

  return _stat_dfile(file->storage, buf);
}

int __fd_stat(const char *path, struct stat64 *buf) {
  disk_file_t *dfile = __get_sym_file(path);

  if (!dfile) {
    int res;
#if __WORDSIZE == 64
    res = syscall(__NR_stat, __concretize_string(path), buf);
#else
    res = syscall(__NR_stat64, __concretize_string(path), buf);
#endif
    if (res == -1)
      errno = klee_get_errno();
    return res;
  }

  return _stat_dfile(dfile, buf);
}

// path is assumed to be nonnull. This is checked via compiler attribute
// "__nonnull__"
// FIXME: this should be put into fd_32 and fd_64 as well. Currently I force a
// struct stat -> struct stat64 conversion
int fstatat(int dirfd, const char *path, struct stat *buf, int flags) {  
  if (dirfd != AT_FDCWD) {
    fd_entry_t *bf = __get_fd(dirfd);

    if (!bf || !(bf->attr & eIsFile)) {
      errno = EBADF;
      return -1;
    }
    file_t *bfile = (file_t*)(bf->io_object);
    if (!_file_is_concrete(bfile)) {
      klee_warning("symbolic file descriptor, ignoring (ENOENT)");
      errno = ENOENT;
      return -1;
    }
    dirfd = bfile->concrete_fd;
  }
  disk_file_t *dfile = __get_sym_file(path);

  if (dfile) {
    return _stat_dfile(dfile, (struct stat64*)buf);
  } 

  int res;
#if (defined __NR_newfstatat) && (__NR_newfstatat != 0)
  res = syscall(__NR_newfstatat, (long)dirfd,
                 __concretize_string(path), buf, (long)flags);
#else
  res = syscall(__NR_fstatat64, (long)dirfd,
                 __concretize_string(path), buf, (long)flags);
#endif
  if (res == -1)
    errno = klee_get_errno();
  return res;
}

int __fd_lstat(const char *path, struct stat64 *buf) {
  disk_file_t *dfile = __get_sym_file(path);

  if (!dfile) {
    int res;
#if __WORDSIZE == 64
    res = syscall(__NR_lstat, __concretize_string(path), buf);
#else
    res = syscall(__NR_lstat64, __concretize_string(path), buf);
#endif
    if (res == -1)
      errno = klee_get_errno();
    return res;
  } else {
    return _stat_dfile(dfile, buf);
  }
}

/* Do I have to port this?
int __fd_fstat(int fd, struct stat64 *buf) {
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }
  // TODO
}
*/
////////////////////////////////////////////////////////////////////////////////

#if __WORDSIZE == 64
int _ioctl_file(file_t *file, unsigned long int request, char *argp) {
#else
int _ioctl_file(file_t *file, unsigned long request, char *argp) {
#endif
  if (_file_is_concrete(file)) {
    // ioctl to a concrete file
    int res;
    res = CALL_UNDERLYING(ioctl, file->concrete_fd, request, argp);
    if (res == -1) {
      errno = klee_get_errno();
    }
    return res;
  } else {
    // ioctl to a symbolic file
    struct stat *stat = (struct stat *)file->storage->stat;
    switch (request) {
    case TCGETS: {
      struct termios *ts = (struct termios*)argp;

      klee_warning_once("(TCGETS) symbolic file, incomplete model");

      /* XXX need more data, this is ok but still not good enough */
      if (S_ISCHR(stat->st_mode)) {
        /* Just copied from my system, munged to match what fields
           uclibc thinks are there. */
        ts->c_iflag = 27906;
        ts->c_oflag = 5;
        ts->c_cflag = 1215;
        ts->c_lflag = 35287;
#ifdef __GLIBC__
        ts->c_line = 0;
#endif
        ts->c_cc[0] = '\x03';
        ts->c_cc[1] = '\x1c';
        ts->c_cc[2] = '\x7f';
        ts->c_cc[3] = '\x15';
        ts->c_cc[4] = '\x04';
        ts->c_cc[5] = '\x00';
        ts->c_cc[6] = '\x01';
        ts->c_cc[7] = '\xff';
        ts->c_cc[8] = '\x11';
        ts->c_cc[9] = '\x13';
        ts->c_cc[10] = '\x1a';
        ts->c_cc[11] = '\xff';
        ts->c_cc[12] = '\x12';
        ts->c_cc[13] = '\x0f';
        ts->c_cc[14] = '\x17';
        ts->c_cc[15] = '\x16';
        ts->c_cc[16] = '\xff';
        ts->c_cc[17] = '\x0';
        ts->c_cc[18] = '\x0';
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case TCSETS: {
      /* const struct termios *ts = argp; */
      klee_warning_once("(TCSETS) symbolic file, silently ignoring");
      if (S_ISCHR(stat->st_mode)) {
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case TCSETSW: {
      /* const struct termios *ts = argp; */
      klee_warning_once("(TCSETSW) symbolic file, silently ignoring");
      if (file->storage == __sym_fs.sym_stdin) {
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case TCSETSF: {
      /* const struct termios *ts = argp; */
      klee_warning_once("(TCSETSF) symbolic file, silently ignoring");
      if (S_ISCHR(stat->st_mode)) {
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case TIOCGWINSZ: {
      struct winsize *ws = (struct winsize*)argp;
      ws->ws_row = 24;
      ws->ws_col = 80;
      klee_warning_once("(TIOCGWINSZ) symbolic file, incomplete model");
      if (S_ISCHR(stat->st_mode)) {
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case TIOCSWINSZ: {
      /* const struct winsize *ws = argp; */
      klee_warning_once("(TIOCSWINSZ) symbolic file, ignoring (EINVAL)");
      if (S_ISCHR(stat->st_mode)) {
        errno = EINVAL;
        return -1;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case FIONREAD: {
      int *res = (int*)argp;
      klee_warning_once("(FIONREAD) symbolic file, incomplete model");
      if (S_ISCHR(stat->st_mode)) {
        if (file->offset < (off64_t)file->storage->size) {
          *res = file->storage->size - file->offset;
        } else {
          *res = 0;
        }
        return 0;
      } else {
        errno = ENOTTY;
        return -1;
      }
    }
    case MTIOCGET: {
      klee_warning("(MTIOCGET) symbolic file, ignoring (EINVAL)");
      errno = EINVAL;
      return -1;
    }
    default:
      klee_warning("symbolic file, ignoring (EINVAL)");
      errno = EINVAL;
      return -1;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

/* Returns 1 if the process has the access rights specified by 'flags'
   to the file with stat 's'.  Returns 0 otherwise*/
static int _can_open(int flags, const struct stat64 *s) {
  int write_access, read_access;
  mode_t mode = s->st_mode;

  if ((flags & O_ACCMODE) != O_WRONLY)
    read_access = 1;
  else
    read_access = 0;

  if ((flags & O_ACCMODE) != O_RDONLY)
    write_access = 1;
  else
    write_access = 0;

  /* XXX: We don't worry about process uid and gid for now.
     We allow access if any user has access to the file. */
#if 0
  uid_t uid = s->st_uid;
  uid_t euid = geteuid();
  gid_t gid = s->st_gid;
  gid_t egid = getegid();
#endif

  if (read_access && !(mode & (S_IRUSR | S_IRGRP | S_IROTH)))
    return 0;

  if (write_access && !(mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
    return 0;

  return 1;
}

int __fd_open(const char *pathname, int flags, mode_t mode) {
  const char *cpathname = __concretize_string(pathname);
  posix_debug_msg("Attempting to open: %s\n", cpathname);
  // Obtain a symbolic file
  disk_file_t *dfile = __get_sym_file(pathname);

  if (dfile) {
    return _open_symbolic(dfile, flags, mode);
  } else {
    if ((flags & O_ACCMODE) != O_RDONLY && !__sym_fs.allow_unsafe) {
      klee_warning("blocked non-r/o access to concrete file");
      errno = EACCES;
      return -1;
    }

    int concrete_fd = syscall(__NR_open, cpathname, flags, mode);

    if (concrete_fd == -1) {
      errno = klee_get_errno();
      return -1;
    }

    int fd = _open_concrete(concrete_fd, flags);

    if (fd == -1) {
      syscall(__NR_close, concrete_fd);
      return -1;
    }

    posix_debug_msg("%s opened as %d\n", cpathname, fd);
    return fd;
  }
}

int _open_concrete(int concrete_fd, int flags) {
  int fd = __fd_allocate();

  if (fd == MAX_FDS) {
    errno = ENFILE;
    return -1;
  }

  fd_entry_t *fde = &__exe_env.fds[fd];
  fde->attr |= eIsFile;

  file_t *file = (file_t*)malloc(sizeof(file_t));
  klee_make_shared(file, sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->__bdata.flags = flags;
  file->__bdata.refcount = 1;
  file->storage = NULL;
  file->concrete_fd = concrete_fd;
  file->offset = 0;

  fde->io_object = (file_base_t*)file;
  /*
   * I think we do not need the following model.
  // Check to see if the concrete FD is a char/PIPE/socket
  struct stat s;
  int res = CALL_UNDERLYING(fstat, concrete_fd, &s);
  assert(res == 0);

  if ((S_ISREG(s.st_mode) && !_fs.overlapped_writes) || (S_ISCHR(s.st_mode) || S_ISFIFO(s.st_mode) ||
      S_ISSOCK(s.st_mode))) {
    file->offset = -1;
  } else {
    file->offset = CALL_UNDERLYING(lseek, concrete_fd, 0, SEEK_CUR);
    assert(file->offset >= 0);
  }
  */

  if (flags & O_CLOEXEC) {
    fde->attr |= eCloseOnExec;
  }

  return fd;
}

int _open_symbolic(disk_file_t *dfile, int flags, mode_t mode) {
  // Checking the flags
  if ((flags & O_CREAT) && (flags & O_EXCL)) {
    errno = EEXIST;
    return -1;
  }

  if ((flags & O_TRUNC) && (flags & O_ACCMODE) == O_RDONLY) {
    /* The result of using O_TRUNC with O_RDONLY is undefined, so we
   return error */
    klee_warning("Undefined call to open(): O_TRUNC | O_RDONLY\n");
    errno = EACCES;
    return -1;
  }

  if ((flags & O_EXCL) && !(flags & O_CREAT)) {
    /* The result of using O_EXCL without O_CREAT is undefined, so
   we return error */
    klee_warning("Undefined call to open(): O_EXCL w/o O_RDONLY\n");
    errno = EACCES;
    return -1;
  }

  if (!_can_open(flags, dfile->stat)) {
    errno = EACCES;
    return -1;
  } else {
    dfile->stat->st_mode =
        ((dfile->stat->st_mode & ~0777) | (mode & ~__exe_env.umask));
    /*    clear existing file mode   |  requested mode - umask */
  }

  // Now we can allocate a FD
  int fd = __fd_allocate();

  if (fd == MAX_FDS) {
    errno = ENFILE;
    return -1;
  }

  fd_entry_t *fde = &__exe_env.fds[fd];
  fde->attr |= eIsFile;

  // Now we can set up the open file structure...
  file_t *file = (file_t*)malloc(sizeof(file_t));
  klee_make_shared(file, sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->__bdata.flags = flags;
  file->__bdata.refcount = 1;
  file->storage = dfile;
  file->offset = 0;
  file->concrete_fd = -1;

  if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_TRUNC)) {
    if (file->storage->ops.truncate) {
      file->storage->ops.truncate(file->storage, 0);
    } else {
      klee_warning("Trunc operation not supported.");
    }
  }

  if (flags & O_APPEND) {
    file->offset = file->storage->stat->st_size;
  }

  if (flags & O_CLOEXEC) {
    fde->attr |= eCloseOnExec;
  }

  fde->io_object = (file_base_t*)file;

  posix_debug_msg("symbolic file opened as %d\n", fd);
  return fd;
}

int __fd_openat(int basefd, const char *pathname, int flags, mode_t mode) {
  if (basefd != AT_FDCWD) {
    fd_entry_t *bf = __get_fd(basefd);

    if (!bf || !(bf->attr & eIsFile)) {
      errno = EBADF;
      return -1;
    }
    file_t *bfile = (file_t*)(bf->io_object);
    if (!_file_is_concrete(bfile)) {
      klee_warning("symbolic file descriptor, ignoring (ENOENT)");
      errno = ENOENT;
      return -1;
    }
    basefd = bfile->concrete_fd;
  }

  if (__get_sym_file(pathname)) {
    /* for a symbolic file, it doesn't matter if/where it exists on disk */
    return __fd_open(pathname, flags, mode);
  }

  int os_fd = syscall(__NR_openat, (long)basefd, __concretize_string(pathname), (long)flags, mode);
  if (os_fd == -1) {
    errno = klee_get_errno();
    return -1;
  }

  int fd = _open_concrete(os_fd, flags);
  if (fd == -1) {
    syscall(__NR_close, os_fd);
    return -1;
  }

  return fd;
}

DEFINE_MODEL(int, creat, const char *pathname, mode_t mode) {
  return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int utimes(const char *path, const struct timeval times[2]) {
  disk_file_t *dfile = __get_sym_file(path);

  if (dfile) {

    if (!times) {
      struct timeval newTimes[2];
      gettimeofday(&(newTimes[0]), NULL);
      newTimes[1] = newTimes[0];
      times = newTimes;
    }

    /* don't bother with usecs */
    dfile->stat->st_atime = times[0].tv_sec;
    dfile->stat->st_mtime = times[1].tv_sec;
#ifdef _BSD_SOURCE
    dfile->stat->st_atim.tv_nsec = 1000000000ll * times[0].tv_sec;
    dfile->stat->st_mtim.tv_nsec = 1000000000ll * times[1].tv_sec;
#endif
    return 0;
  }
  return syscall(__NR_utimes, __concretize_string(path), times);
}

int futimesat(int fd, const char* path, const struct timeval times[2]) {
  if (fd != AT_FDCWD) {
    fd_entry_t *fde = __get_fd(fd);

    if (!fde || !(fde->attr & eIsFile)) {
      errno = EBADF;
      return -1;
    }
    file_t *f = (file_t*)(fde->io_object);
    if (!_file_is_concrete(f)) {
      klee_warning("symbolic file descriptor, ignoring (ENOENT)");
      errno = ENOENT;
      return -1;
    }
    fd = f->concrete_fd;
  }
  if (__get_sym_file(path)) {
    return utimes(path, times);
  }

  return syscall(__NR_futimesat, (long)fd,
                 (path ? __concretize_string(path) : NULL), times);
}

////////////////////////////////////////////////////////////////////////////////

int _close_file(file_t *file) {
  free(file);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int __fd_ftruncate(int fd, off64_t length) {
  static int n_calls = 0;
  n_calls++;
  if (__sym_fs.max_failures && *__sym_fs.ftruncate_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }
  fd_entry_t *fde;
  CHECK_IS_FILE(fd, fde);
  file_t *file = (file_t*)fde->io_object;
  if (_file_is_concrete(file)) {
#if __WORDSIZE == 64
  return syscall(__NR_ftruncate, file->concrete_fd, length);
#else
  return syscall(__NR_ftruncate64, file->concrete_fd, length);
#endif
  } else {
    klee_warning("symbolic file fd_ftruncate, ignoring (EIO)");
    errno = EIO;
    return -1;
  }
}
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(char *, getcwd, char *buf, size_t size) {
  static int n_calls = 0;
  int r;

  n_calls++;

  if (__sym_fs.max_failures && *__sym_fs.getcwd_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = ERANGE;
    return NULL;
  }

  if (!buf) {
    if (!size)
      size = 1024;
    buf = malloc(size);
  }

  buf = __concretize_ptr(buf);
  size = __concretize_size(size);
  /* XXX In terms of looking for bugs we really should do this check
     before concretization, at least once the routine has been fixed
     to properly work with symbolics. */
  klee_check_memory_access(buf, size);
  r = CALL_UNDERLYING(getcwd, buf, size);

  if (r == -1) {
    errno = klee_get_errno();
    return NULL;
  }

  return buf;
}

////////////////////////////////////////////////////////////////////////////////

static off64_t _lseek(file_t *file, off64_t offset, int whence) {
  off64_t newOff;
  switch (whence) {
  case SEEK_SET:
    newOff = offset;
    break;
  case SEEK_CUR:
    newOff = file->offset + offset;
    break;
  case SEEK_END:
    newOff = file->storage->stat->st_size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  if (newOff < 0 || newOff > file->storage->stat->st_size) {
    errno = EINVAL;
    return -1;
  }

  file->offset = newOff;
  return file->offset;
}

off64_t __fd_lseek64(int fd, off64_t offset, int whence) {
  fd_entry_t *fde;
  CHECK_IS_FILE(fd, fde);

  file_t *file = (file_t*)fde->io_object;

  if (_file_is_concrete(file)) {
    off64_t new_off;
    offset = __concretize_offset(offset);
    /* We could always do SEEK_SET then whence, but this causes
       troubles with directories since we play nasty tricks with the
       offset, and the OS doesn't want us to randomly seek
       directories. We could detect if it is a directory and correct
       the offset, but really directories should only be SEEK_SET, so
       this solves the problem. */
    if (whence == SEEK_SET) {
      new_off = syscall(__NR_lseek, file->concrete_fd, offset, SEEK_SET);
    } else {
      new_off = syscall(__NR_lseek, file->concrete_fd, file->offset, SEEK_SET);

      /* If we can't seek to start off, just return same error.
         Probably ESPIPE. */
      if (new_off != -1) {
        assert(new_off == file->offset);
        new_off = syscall(__NR_lseek, file->concrete_fd, offset, whence);
      }
    }

    if (new_off == -1) {
      errno = klee_get_errno();
    } else {
      if (file->offset == -1) {
        posix_debug_msg("lseek attempted on stream fd (or non-overlapping "
                        "mode?). Concrete offset not recorded.");
      } else {
        file->offset = new_off;
      }
    }
    return new_off;
  }

  return _lseek(file, offset, whence);
}

////////////////////////////////////////////////////////////////////////////////

/* Sets mode and or errno and return appropriate result. */
static int _df_chmod(disk_file_t *dfile, mode_t mode) {
  if (geteuid() == dfile->stat->st_uid) {
    if (getgid() != dfile->stat->st_gid)
      mode &= ~ S_ISGID;
    dfile->stat->st_mode = ((dfile->stat->st_mode & ~07777) |
                         (mode & 07777));
    return 0;
  } else {
    errno = EPERM;
    return -1;
  }
}

DEFINE_MODEL(int, chmod, const char *path, mode_t mode) {
  static int n_calls = 0;
  n_calls++;
  if (__sym_fs.max_failures && *__sym_fs.chmod_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }
  disk_file_t *dfile = __get_sym_file(path);

  if (!dfile) {
    int res = CALL_UNDERLYING(chmod, __concretize_string(path), mode);
    if (res == -1)
      errno = klee_get_errno();
    return res;
  }

  return _df_chmod(dfile, mode);
}

DEFINE_MODEL(int, fchmod, int fd, mode_t mode) {
  static int n_calls = 0;
  n_calls++;
  if (__sym_fs.max_failures && *__sym_fs.fchmod_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }
  fd_entry_t *fde;
  CHECK_IS_FILE(fd, fde);

  file_t *file = (file_t*)fde->io_object;

  if (_file_is_concrete(file)) {
    int res = CALL_UNDERLYING(fchmod, file->concrete_fd, mode);

    if (res == -1)
      errno = klee_get_errno();

    return res;
  }

  return _df_chmod(file->storage, mode);
}


////////////////////////////////////////////////////////////////////////////////
// Directory management
////////////////////////////////////////////////////////////////////////////////

// getdents is currently broken. Since we support customized symbolic file name,
// I need to change the ways of creating new dirent entry and calculate the
// offset, reclen carefully.
// Besides, I have not looked into getdents' behaviour.
int __fd_getdents(unsigned int fd, struct dirent64 *dirp, unsigned int count) {
  fd_entry_t *fde;
  CHECK_IS_FILE(fd, fde);

  file_t *file = (file_t*)fde->io_object;

  if (!_file_is_concrete(file)) {
    klee_warning("symbolic file, ignoring (EINVAL)");
    errno = EINVAL;
    return -1;
  }

  klee_warning("getdents is broken now, you need to fix this before using it");

  if ((unsigned long) file->offset < 4096u) {
    /* Return our dirents */
    unsigned i, pad, bytes=0;

    /* What happens for bad offsets? */
    i = file->offset / sizeof(*dirp);
    if (((off64_t) (i * sizeof(*dirp)) != file->offset) ||
        i > __sym_fs.n_sym_files) {
      errno = EINVAL;
      return -1;
    }

    for (; i< __sym_fs.n_sym_files; ++i) {
      disk_file_t *df = &__sym_fs.sym_files[i];

      dirp->d_ino = df->stat->st_ino;
      dirp->d_reclen = sizeof(*dirp);
      dirp->d_type = IFTODT(df->stat->st_mode);
      dirp->d_name[0] = 'A' + i;
      dirp->d_name[1] = '\0';
#ifdef _DIRENT_HAVE_D_OFF
      dirp->d_off = (i+1) * sizeof(*dirp);
#endif
      bytes += dirp->d_reclen;
      ++dirp;
    }

    /* Fake jump to OS records by a "deleted" file. */
    pad = count>=4096 ? 4096 : count;
    dirp->d_ino = 0;
    dirp->d_reclen = pad - bytes;
    dirp->d_type = DT_UNKNOWN;
    dirp->d_name[0] = '\0';
#ifdef _DIRENT_HAVE_D_OFF
    dirp->d_off = 4096;
#endif
    bytes += dirp->d_reclen;
    file->offset = pad;
    return bytes;
  } else {
    unsigned os_pos = file->offset - 4096;
    int res, s;

    /* For reasons which I really don't understand, if I don't
       memset this then sometimes the kernel returns d_ino==0 for
       some valid entries? Am I crazy? Can writeback possibly be
       failing?

       Even more bizarre, interchanging the memset and the seek also
       case strange behavior. Really should be debugged properly. */
    memset(dirp, 0, count);
    s = syscall(__NR_lseek, file->concrete_fd, (int) os_pos, SEEK_SET);
    assert(s != (off64_t) -1);
    res = syscall(__NR_getdents64, file->concrete_fd, dirp, count);
    if (res == -1) {
      errno = klee_get_errno();
    } else {
      int pos = 0;

      file->offset = syscall(__NR_lseek, file->concrete_fd, 0, SEEK_CUR) + 4096;

      /* Patch offsets */

      while (pos < res) {
        struct dirent64 *dp = (struct dirent64*) ((char*) dirp + pos);
#ifdef _DIRENT_HAVE_D_OFF
        dp->d_off += 4096;
#endif
        pos += dp->d_reclen;
      }
    }
    return res;
  }
}

/* I do not know what are the following alias for
int __getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
     __attribute__((alias("getdents")));

int getdents64(unsigned int fd, struct dirent *dirp, unsigned int count) {
  return getdents(fd, (struct dirent64*) dirp, count);
}

int __getdents64(unsigned int fd, struct dirent *dirp, unsigned int count)
     __attribute__((alias("getdents64")));
*/

int rmdir(const char *pathname) {
  disk_file_t *dfile = __get_sym_file(pathname);
  if (dfile) {
    /* XXX check access */ 
    if (S_ISDIR(dfile->stat->st_mode)) {
      dfile->stat->st_ino = 0;
      return 0;
    } else {
      errno = ENOTDIR;
      return -1;
    }
  }

  klee_warning("ignoring (EPERM)");
  errno = EPERM;
  return -1;
}

int unlink(const char *pathname) {
  disk_file_t *dfile = __get_sym_file(pathname);
  if (dfile) {
    /* XXX check access */ 
    if (S_ISREG(dfile->stat->st_mode)) {
      dfile->stat->st_ino = 0;
      return 0;
    } else if (S_ISDIR(dfile->stat->st_mode)) {
      errno = EISDIR;
      return -1;
    } else {
      errno = EPERM;
      return -1;
    }
  }

  klee_warning("ignoring (EPERM)");
  errno = EPERM;
  return -1;
}

int unlinkat(int dirfd, const char *pathname, int flags) {
  /* similar to unlink. keep them separated though to avoid
     problems if unlink changes to actually delete files */
  disk_file_t *dfile = __get_sym_file(pathname);
  if (dfile) {
    /* XXX check access */ 
    if (S_ISREG(dfile->stat->st_mode)) {
      dfile->stat->st_ino = 0;
      return 0;
    } else if (S_ISDIR(dfile->stat->st_mode)) {
      errno = EISDIR;
      return -1;
    } else {
      errno = EPERM;
      return -1;
    }
  }

  klee_warning("ignoring (EPERM)");
  errno = EPERM;
  return -1;
}

////////////////////////////////////////////////////////////////////////////////

#define _WRAP_FILE_SYSCALL_ERROR(call, ...)                                    \
  do {                                                                         \
    if (__get_sym_file(pathname)) {                                            \
      klee_warning("symbolic path, " #call " unsupported (ENOENT)");           \
      errno = ENOENT;                                                          \
      return -1;                                                               \
    }                                                                          \
    int ret =                                                                  \
        CALL_UNDERLYING(call, __concretize_string(pathname), ##__VA_ARGS__);   \
    if (ret == -1)                                                             \
      errno = klee_get_errno();                                                \
    return ret;                                                                \
  } while (0)

#define _WRAP_FILE_SYSCALL_IGNORE(call, ...)                                   \
  do {                                                                         \
    if (__get_sym_file(pathname)) {                                            \
      klee_warning("symbolic path, " #call " does nothing");                   \
      return 0;                                                                \
    }                                                                          \
    int ret =                                                                  \
        CALL_UNDERLYING(call, __concretize_string(pathname), ##__VA_ARGS__);   \
    if (ret == -1)                                                             \
      errno = klee_get_errno();                                                \
    return ret;                                                                \
  } while (0)

#define _WRAP_FILE_SYSCALL_BLOCK(call, ...)                                    \
  do {                                                                         \
    klee_warning(#call " blocked (EPERM)");                                    \
    errno = EPERM;                                                             \
    return -1;                                                                 \
  } while (0)

DEFINE_MODEL(ssize_t, readlink, const char *pathname, char *buf, size_t bufsize) {
  _WRAP_FILE_SYSCALL_ERROR(readlink, buf, bufsize);
}

DEFINE_MODEL(int, chroot, const char *pathname) {
  _WRAP_FILE_SYSCALL_BLOCK(chroot);
}

DEFINE_MODEL(int, chown, const char *pathname, uid_t owner, gid_t group) {
  _WRAP_FILE_SYSCALL_ERROR(chown, owner, group);
}

DEFINE_MODEL(int, lchown, const char *pathname, uid_t owner, gid_t group) {
  /* XXX Ignores 'l' part */
  _WRAP_FILE_SYSCALL_ERROR(lchown, owner, group);
}

DEFINE_MODEL(int, chdir, const char *pathname) {
  _WRAP_FILE_SYSCALL_ERROR(chdir);
}

////////////////////////////////////////////////////////////////////////////////

#define _WRAP_FD_SYSCALL_ERROR(call, ...)                                      \
  do {                                                                         \
    fd_entry_t *fde;                                                           \
    CHECK_IS_FILE(fd, fde);                                                    \
    file_t *file = (file_t *)fde->io_object;                                   \
    if (!_file_is_concrete(file)) {                                            \
      klee_warning("symbolic file, " #call " unsupported (EBADF)");            \
      errno = EBADF;                                                           \
      return -1;                                                               \
    }                                                                          \
    int ret = CALL_UNDERLYING(call, file->concrete_fd, ##__VA_ARGS__);         \
    if (ret == -1)                                                             \
      errno = klee_get_errno();                                                \
    return ret;                                                                \
  } while (0)

#define _WRAP_FD_SYSCALL_IGNORE(call, ...)                                     \
  do {                                                                         \
    fd_entry_t *fde;                                                           \
    CHECK_IS_FILE(fd, fde);                                                    \
    file_t *file = (file_t *)fde->io_object;                                   \
    if (!_file_is_concrete(file)) {                                            \
      klee_warning("symbolic file, " #call " does nothing");                   \
      return 0;                                                                \
    }                                                                          \
    int ret = CALL_UNDERLYING(call, file->concrete_fd, ##__VA_ARGS__);         \
    if (ret == -1)                                                             \
      errno = klee_get_errno();                                                \
    return ret;                                                                \
  } while (0)

DEFINE_MODEL(int, fsync, int fd) {
  _WRAP_FD_SYSCALL_IGNORE(fsync);
}

DEFINE_MODEL(int, fdatasync, int fd) {
  _WRAP_FD_SYSCALL_IGNORE(fdatasync);
}

DEFINE_MODEL(int, fchdir, int fd) {
  _WRAP_FD_SYSCALL_ERROR(fchdir);
}

DEFINE_MODEL(int, fchown, int fd, uid_t owner, gid_t group) {
  _WRAP_FD_SYSCALL_ERROR(fchown, owner, group);
}

DEFINE_MODEL(int, fstatfs, int fd, struct statfs *buf) {
  _WRAP_FD_SYSCALL_ERROR(fstatfs, buf);
}

DEFINE_MODEL(int, statfs, const char *pathname, struct statfs *buf) {
  _WRAP_FILE_SYSCALL_ERROR(statfs, buf);
}

DEFINE_MODEL(int, truncate, const char *pathname, off_t length) {
  _WRAP_FILE_SYSCALL_ERROR(truncate, length);
}

DEFINE_MODEL(int, access, const char *pathname, int mode) {
  _WRAP_FILE_SYSCALL_IGNORE(access, mode);
}
