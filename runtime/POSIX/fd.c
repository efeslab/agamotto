//===-- fd.c --------------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define _LARGEFILE64_SOURCE
#include "fd.h"
#include "sockets.h"
#include "pipes.h"
#include "buffers.h"
#include "files.h"
#include "symfs.h"
#include "misc.h"

#include "klee/klee.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#ifndef __FreeBSD__
#include <sys/vfs.h>
#endif
#include <dirent.h>
#include <sys/select.h>
#include <unistd.h>
// Forward declaration
static int _dup(int oldfd, int startfd);
// For multiplexing multiple syscalls into a single _read/_write op. set
#define _IO_TYPE_SCATTER_GATHER  0x1
#define _IO_TYPE_POSITIONAL      0x2
////////////////////////////////////////////////////////////////////////////////
// Internal routines
////////////////////////////////////////////////////////////////////////////////

/* Returns pointer to the file entry for a valid fd */
fd_entry_t *__get_fd(int fd) {
  if (fd>=0 && fd<MAX_FDS) {
    fd_entry_t *f = &__exe_env.fds[fd];
    if (f->attr & eOpen)
      return f;
  }
  return NULL;
}

mode_t umask(mode_t mask) {  
  mode_t r = __exe_env.umask;
  __exe_env.umask = mask & 0777;
  return r;
}

int __fd_allocate() {
  int fd;
  for (fd = 0; fd < MAX_FDS; ++fd) {
    if (!(__exe_env.fds[fd].attr & eOpen)) {
      fd_entry_t *f = &__exe_env.fds[fd];
      assert(f->attr == 0 && !f->io_object);
      f->attr |= eOpen;
      break;
    }
  }
  return fd;
}
// note: this does not free io_object
void __fd_clear(int fd) {
  fd_entry_t *f = __get_fd(fd);
  assert(f != NULL);
  memset(f, 0, sizeof(*f));
}

static int _is_blocking(int fd, int event) {
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    return 0;
  }

  switch (event) {
  case EVENT_READ:
    if ((fde->io_object->flags & O_ACCMODE) == O_WRONLY) {
      return 0;
    }
    break;
  case EVENT_WRITE:
    if ((fde->io_object->flags & O_ACCMODE) == O_RDONLY) {
      return 0;
    }
    break;
  default:
    assert(0 && "invalid event");
  }

  if (fde->attr & eIsFile) {
    return _is_blocking_file((file_t*)fde->io_object, event);
  } else if (fde->attr & eIsPIPE) {
    return _is_blocking_pipe((pipe_end_t*)fde->io_object, event);
  } else if (fde->attr & eIsSocket) {
    return _is_blocking_socket((socket_t*)fde->io_object, event);
  } else {
    assert(0 && "invalid fd");
  }
}

/*
 * Normal read/write
 */

// input fd is guaranteed to be valid
static ssize_t _clean_read(int fd, void *buf, size_t count, off64_t offset) {
  static int n_calls = 0;
  n_calls++;
  fd_entry_t *fde = &__exe_env.fds[fd];

  if (offset >= 0 && !(fde->attr & eIsFile)) {
    errno = EINVAL;
    return -1;
  }
  if (buf == NULL) {
    errno = EFAULT;
    return -1;
  }
  if (count == 0)
    return 0;
  if (__sym_fs.max_failures && *__sym_fs.read_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }

  posix_debug_msg("Reading %lu bytes of data from FD %d\n", count, fd);
  if (fde->attr & eIsFile) {
    return _read_file((file_t *)fde->io_object, buf, count, offset);
  } else if (fde->attr & eIsPIPE) {
    return _read_pipe((pipe_end_t *)fde->io_object, buf, count);
  } else if (fde->attr & eIsSocket) {
    return _read_socket((socket_t *)fde->io_object, buf, count);
  } else {
    assert(0 && "Invalid file descriptor");
    return 0;
  }
}

// input fd is guaranteed to be valid
static ssize_t _clean_write(int fd, const void *buf, size_t count, off_t offset) {
  static int n_calls = 0;
  n_calls++;
  fd_entry_t *fde = &__exe_env.fds[fd];

  if (offset >= 0 && !(fde->attr & eIsFile)) {
    errno = EINVAL;
    return -1;
  }

  if (__sym_fs.max_failures && *__sym_fs.write_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }

  if (fde->attr & eIsFile) {
    return _write_file((file_t*)fde->io_object, buf, count, offset);
  } else if (fde->attr & eIsPIPE) {
    return _write_pipe((pipe_end_t*)fde->io_object, buf, count);
  } else if (fde->attr & eIsSocket) {
    return _write_socket((socket_t*)fde->io_object, buf, count);
  } else {
    assert(0 && "Invalid file descriptor");
    return 0;
  }
}
/*
 * Vectorized scatter read / gather write
 */
ssize_t _scatter_read(int fd, const struct iovec *iov, int iovcnt) {
  size_t count = 0;

  int i;
  for (i = 0; i < iovcnt; i++) {
    if (iov[i].iov_len == 0)
      continue;

    if (count > 0 && _is_blocking(fd, EVENT_READ))
      return count;

    ssize_t res = _clean_read(fd, iov[i].iov_base, iov[i].iov_len, -1);

    if (res == -1) {
      assert(count == 0);
      return res;
    }

    count += res;

    if ((size_t)res < iov[i].iov_len)
      break;
  }

  return count;
}

ssize_t _gather_write(int fd, const struct iovec *iov, int iovcnt) {
  size_t count = 0;

  int i;
  for (i = 0; i < iovcnt; i++) {
    if (iov[i].iov_len == 0)
      continue;

    // If we have something written, but now we blocked, we just return
    // what we have
    if (count > 0 && _is_blocking(fd, EVENT_WRITE))
      return count;

    ssize_t res = _clean_write(fd, iov[i].iov_base, iov[i].iov_len, -1);

    if (res == -1) {
      assert(count == 0);
      return res;
    }

    count += res;

    if ((size_t)res < iov[i].iov_len)
      break;
  }

  return count;
}

////////////////////////////////////////////////////////////////////////////////
// FD specific POSIX routines
////////////////////////////////////////////////////////////////////////////////

int close(int fd) {
  static int n_calls = 0;
  fd_entry_t *fde;
  
  n_calls++;  

  fde = __get_fd(fd);
  if (!fde) {
    errno = EBADF;
    return -1;
  } 
  posix_debug_msg("Attempting to close %d, attr %#x\n", fd, fde->attr);

  if (__sym_fs.max_failures && *__sym_fs.close_fail == n_calls) {
    __sym_fs.max_failures--;
    errno = EIO;
    return -1;
  }

  if (fde->io_object->refcount > 1) {
    // Just clear this FD
    fde->io_object->refcount--;
    __fd_clear(fd);
    return 0;
  }
  int res;
  // Check the type of the file descriptor
  if (fde->attr & eIsFile) {
    if (fd >= 0 && fd <= 2) {
      // special fds, statically initialized, should not be freed
      res = 0;
    } else {
      res = _close_file((file_t *)(fde->io_object));
    }
  } else if (fde->attr & eIsPIPE) {
    res = _close_pipe((pipe_end_t*)fde->io_object);
  } else if (fde->attr & eIsSocket) {
    res = _close_socket((socket_t*)fde->io_object);
  } else {
    assert(0 && "Invalid file descriptor");
    return -1;
  }

  if (res == 0) {
    __fd_clear(fd);
  }
  
  return res;
}

/*
 * Read related POSIX API
 */
static ssize_t _read(int fd, int type, ...) {
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }


  // Check for permissions
  if ((fde->io_object->flags & O_ACCMODE) == O_WRONLY) {
    posix_debug_msg("Permission error (flags: %o)\n", fde->io_object->flags);
    errno = EBADF;
    return -1;
  }
  if (INJECT_FAULT(read, EINTR)) {
    return -1;
  }

  if (fde->attr & eIsFile) {
    if (INJECT_FAULT(read, EIO)) {
      return -1;
    }
  } else if (fde->attr & eIsSocket) {
    socket_t *sock = (socket_t*)fde->io_object;

    if (sock->status == SOCK_STATUS_CONNECTED &&
        INJECT_FAULT(read, ECONNRESET)) {
      return -1;
    }
  }

  // Now perform the real thing
  if (type == _IO_TYPE_SCATTER_GATHER) {
    va_list ap;
    va_start(ap, type);
    struct iovec *iov = va_arg(ap, struct iovec*);
    int iovcnt = va_arg(ap, int);
    va_end(ap);

    return _scatter_read(fd, iov, iovcnt);
  } else {
    va_list ap;
    va_start(ap, type);
    void *buf = va_arg(ap, void*);
    size_t count = va_arg(ap, size_t);
    off64_t offset = -1;
    if (type == _IO_TYPE_POSITIONAL)
      offset = va_arg(ap, off64_t);
    va_end(ap);

    return _clean_read(fd, buf, count, offset);
  }
}

DEFINE_MODEL(ssize_t, read, int fd, void *buf, size_t count) {
  return _read(fd, 0, buf, count);
}

DEFINE_MODEL(ssize_t, readv, int fd, const struct iovec *iov, int iovcnt) {
  return _read(fd, _IO_TYPE_SCATTER_GATHER, iov, iovcnt);
}

DEFINE_MODEL(ssize_t, pread64, int fd, void *buf, size_t count, off64_t offset) {
  return _read(fd, _IO_TYPE_POSITIONAL, buf, count, offset);
}

/*
 * Write related POSIX API
 */

static ssize_t _write(int fd, int type, ...) {
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }

  // Check for permissions
  if ((fde->io_object->flags & O_ACCMODE) == O_RDONLY) {
    errno = EBADF;
    return -1;
  }
  if (INJECT_FAULT(write, EINTR)) {
    return -1;
  }

  if (fde->attr & eIsFile) {
    if (INJECT_FAULT(write, EIO, EFBIG, ENOSPC)) {
      return -1;
    }
  } else if (fde->attr & eIsSocket) {
    socket_t *sock = (socket_t*)fde->io_object;

    if (sock->status == SOCK_STATUS_CONNECTED &&
        INJECT_FAULT(write, ECONNRESET, ENOMEM)) {
      return -1;
    }
  }

  if (type == _IO_TYPE_SCATTER_GATHER) {
    va_list ap;
    va_start(ap, type);
    struct iovec *iov = va_arg(ap, struct iovec*);
    int iovcnt = va_arg(ap, int);
    va_end(ap);

    return _gather_write(fd, iov, iovcnt);
  } else {
    va_list ap;
    va_start(ap, type);
    void *buf = va_arg(ap, void*);
    size_t count = va_arg(ap, size_t);
    off_t offset = -1;
    if (type == _IO_TYPE_POSITIONAL)
      offset = va_arg(ap, off_t);
    va_end(ap);

    return _clean_write(fd, buf, count, offset);
  }
}

DEFINE_MODEL(ssize_t, write, int fd, const void *buf, size_t count) {
  return _write(fd, 0, buf, count);
}

DEFINE_MODEL(ssize_t, writev, int fd, const struct iovec *iov, int iovcnt) {
  return _write(fd, _IO_TYPE_SCATTER_GATHER, iov, iovcnt);
}

DEFINE_MODEL(ssize_t, pwrite, int fd, const void *buf, size_t count, off_t offset) {
  return _write(fd, _IO_TYPE_POSITIONAL, buf, count, offset);
}

int __fd_fstat(int fd, struct stat64 *buf) {
  fd_entry_t *fde = __get_fd(fd);

  if (!fde) {
    errno = EBADF;
    return -1;
  }
  if (fde->attr & eIsFile) {
    return _stat_file((file_t*)fde->io_object, buf);
  } else if (fde->attr & eIsPIPE) {
    return _stat_pipe((pipe_end_t*)fde->io_object, buf);
  } else if (fde->attr & eIsSocket) {
    return _stat_socket((socket_t*)fde->io_object, buf);
  } else {
    assert(0 && "Invalid file descriptor");
  }
}

#if __WORDSIZE == 64
int ioctl(int fd, unsigned long int request, ...) {
#else
int ioctl(int fd, unsigned long request, ...) {
#endif
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }
  va_list ap;
  va_start(ap, request);
  char *argp = va_arg(ap, char*);
  va_end(ap);
  if (fde->attr & eIsFile) {
    return _ioctl_file((file_t*)fde->io_object, request, argp);
  } else if (fde->attr & eIsSocket) {
    return _ioctl_socket((socket_t*)fde->io_object, request, argp);
  } else {
    klee_warning("ioctl operation not supported on file descriptor");
    errno = EINVAL;
    return -1;
  }
}

int fcntl(int fd, int cmd, ...) {
  fd_entry_t *fde = __get_fd(fd);
  va_list ap;
  unsigned arg; /* 32 bit assumption (int/ptr) */

  if (!fde) {
    errno = EBADF;
    return -1;
  }
#ifdef F_GETSIG
  if (cmd==F_GETFD || cmd==F_GETFL || cmd==F_GETOWN || cmd==F_GETSIG ||
      cmd==F_GETLEASE || cmd==F_NOTIFY) {
#else
   if (cmd==F_GETFD || cmd==F_GETFL || cmd==F_GETOWN) {
#endif
    arg = 0;
  } else {
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
  }

  // This is the side path, do syscall if fnctl a concrete file
  if (fde->attr & eIsFile) {
    file_t *file = (file_t *)(fde->io_object);
    if (_file_is_concrete(file)) {
      return syscall(__NR_fcntl, file->concrete_fd, cmd, arg);
    }
  }
  switch (cmd) {
  case F_GETFD: {
    int flags = 0;
    if (fde->attr & eCloseOnExec)
      flags |= FD_CLOEXEC;
    return flags;
  }
  case F_SETFD: {
    fde->attr &= ~eCloseOnExec;
    if (arg & FD_CLOEXEC)
      fde->attr |= eCloseOnExec;
    return 0;
  }
  case F_GETFL: {
    return fde->io_object->flags;
  }
  case F_SETFL: {
    if (arg & (O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME)) {
      klee_warning("unsupported fnctl flags");
      errno = EINVAL;
      return -1;
    }
    // This is from cloud9 and I do not understand why doing double check
    // here. (Double check unsupported fnctl flags)
    fde->io_object->flags |=
        (arg & (O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK));
    return 0;
  }
  case F_DUPFD:
  case F_DUPFD_CLOEXEC: {
    int res = _dup(fd, arg);
    if (res < -1) {
      return res;
    }
    if (cmd == F_DUPFD_CLOEXEC) {
      __get_fd(res)->attr |= FD_CLOEXEC;
    }
    return res;
  }
  default:
    klee_warning("symbolic file, ignoring (EINVAL)");
    errno = EINVAL;
    return -1;
  }
}

// dup, dup2, dup3
DEFINE_MODEL(int, dup3, int oldfd, int newfd, int flags) {
  fd_entry_t *fde = __get_fd(oldfd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }

  if (newfd >= MAX_FDS) {
    errno = EBADF;
    return -1;
  }

  if (newfd == oldfd) {
    errno = EINVAL;
    return -1;
  }
  if (INJECT_FAULT(dup, EMFILE, EINTR)) {
    return -1;
  }

  fd_entry_t *newfde = __get_fd(newfd);
  if (newfde) {
    CALL_MODEL(close, newfd);
  }

  __exe_env.fds[newfd] = __exe_env.fds[oldfd];
  newfde = __get_fd(newfd);

  newfde->io_object->refcount++;

  if (flags & O_CLOEXEC) {
    newfde->attr |= eCloseOnExec;
  } else {
    newfde->attr &= ~eCloseOnExec;
  }

  posix_debug_msg("New duplicate of %d: %d, newattr %#x\n", oldfd, newfd,
                  newfde->attr);
  return newfd;
}

DEFINE_MODEL(int, dup2, int oldfd, int newfd) {
  fd_entry_t *fde = __get_fd(oldfd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }

  if (newfd >= MAX_FDS) {
    errno = EBADF;
    return -1;
  }

  if (newfd == oldfd)
    return 0;

  return CALL_MODEL(dup3, oldfd, newfd, 0);
}

static int _dup(int oldfd, int startfd) {
  fd_entry_t *fde = __get_fd(oldfd);
  if (!fde) {
    errno = EBADF;
    return -1;
  }

  if (startfd < 0 || startfd >= MAX_FDS) {
    errno = EBADF;
    return -1;
  }

  // Find the lowest unused file descriptor
  int fd;
  for (fd = startfd; fd < MAX_FDS; fd++) {
    if (!(__exe_env.fds[fd].attr & eOpen))
      break;
  }

  if (fd == MAX_FDS) {
    errno = EMFILE;
    return -1;
  }

  return CALL_MODEL(dup2, oldfd, fd);
}

DEFINE_MODEL(int, dup, int oldfd) {
  return _dup(oldfd, 0);
}

// select() ////////////////////////////////////////////////////////////////////

static int _validate_fd_set(int nfds, fd_set *fds) {
  int res = 0;

  int fd;
  for (fd = 0; fd < nfds; fd++) {
    if (!_FD_ISSET(fd, fds))
      continue;

    fd_entry_t *fde = __get_fd(fd);
    if (!fde) {
      klee_warning("unallocated FD");
      return -1;
    }

    res++;
  }

  return res;
}

static int _register_events(int fd, wlist_id_t wlist, int events) {
  fd_entry_t *fde = __get_fd(fd);
  assert(fde);

  if (fde->attr & eIsPIPE) {
    return _register_events_pipe((pipe_end_t*)fde->io_object, wlist, events);
  } else if (fde->attr & eIsSocket) {
    return _register_events_socket((socket_t*)fde->io_object, wlist, events);
  } else {
    assert(0 && "invalid fd");
  }
}

static void _deregister_events(int fd, wlist_id_t wlist, int events) {
  fd_entry_t *fde = __get_fd(fd);
  if (!fde) {
    // Silently exit
    return;
  }

  if (fde->attr & eIsPIPE) {
    _deregister_events_pipe((pipe_end_t*)fde->io_object, wlist, events);
  } else if (fde->attr & eIsSocket) {
    _deregister_events_socket((socket_t*)fde->io_object, wlist, events);
  } else {
    assert(0 && "invalid fd");
  }
}

// XXX Maybe we should break this into more pieces?
DEFINE_MODEL(int, select, int nfds, fd_set *readfds, fd_set *writefds,
    fd_set *exceptfds, struct timeval *timeout) {

  if (nfds < 0 || nfds > FD_SETSIZE) {
    errno = EINVAL;
    return -1;
  }

  int totalfds = 0;
  static fd_set out_readfds;
  static fd_set out_writefds;
  // Compute the minimum size of the FD set
  int setsize = ((nfds / NFDBITS) + ((nfds % NFDBITS) ? 1 : 0)) * (NFDBITS / 8);


  // Validating the fds
  if (readfds) {
    int res = _validate_fd_set(nfds, readfds);
    if (res ==  -1) {
      errno = EBADF;
      return -1;
    }

    totalfds += res;
    _FD_ZERO(&out_readfds);
  }

  if (writefds) {
    int res = _validate_fd_set(nfds, writefds);
    if (res == -1) {
      errno = EBADF;
      return -1;
    }

    totalfds += res;
    _FD_ZERO(&out_writefds);
  }

  if (exceptfds) {
    int res = _validate_fd_set(nfds, exceptfds);
    if (res == -1) {
      errno = EBADF;
      return -1;
    }

    totalfds += res;
  }

  if (INJECT_FAULT(select, ENOMEM)) {
    return -1;
  }

  if (timeout != NULL && totalfds == 0) {
    klee_warning("simulating timeout");
    // We just return timeout
    if (timeout->tv_sec != 0 || timeout->tv_usec != 0)
      _yield_sleep(timeout->tv_sec, timeout->tv_usec);

    return 0;
  }

  // No out_exceptfds here. This means that the thread will hang if select()
  // is called with FDs only in exceptfds.

  wlist_id_t wlist = 0;
  int res = 0;

  do {
    int fd;
    // First check to see if we have anything available
    for (fd = 0; fd < nfds; fd++) {
      if (readfds && _FD_ISSET(fd, readfds) && !_is_blocking(fd, EVENT_READ)) {
        _FD_SET(fd, &out_readfds);
        res++;
      }
      if (writefds && _FD_ISSET(fd, writefds) && !_is_blocking(fd, EVENT_WRITE)) {
        _FD_SET(fd, &out_writefds);
        res++;
      }
    }

    if (res > 0)
      break;

    // Nope, bad luck...

    // We wait until at least one FD becomes non-blocking

    // In particular, if all FD blocked, then all of them would be
    // valid FDs (none of them closed in the mean time)
    if (wlist == 0)
      wlist = klee_get_wlist();

    int fail = 0;

    // Register ourselves to the relevant FDs
    for (fd = 0; fd < nfds; fd++) {
      int events = 0;
      if (readfds && _FD_ISSET(fd, readfds)) {
        events |= EVENT_READ;
      }
      if (writefds && _FD_ISSET(fd, writefds)) {
        events |= EVENT_WRITE;
      }

      if (events != 0) {
        if (_register_events(fd, wlist, events) == -1) {
          fail = 1;
          break;
        }
      }
    }

    if (!fail)
      __thread_sleep(wlist);

    // Now deregister, in order to avoid useless notifications
    for (fd = 0; fd < nfds; fd++) {
      int events = 0;
      if (readfds && _FD_ISSET(fd, readfds)) {
        events |= EVENT_READ;
      }
      if (writefds && _FD_ISSET(fd, writefds)) {
        events |= EVENT_WRITE;
      }

      if (events != 0) {
        _deregister_events(fd, wlist, events);
      }
    }

    if (fail) {
      errno = ENOMEM;
      return -1;
    }
  } while (1);
  if (readfds) {
    memcpy(readfds, &out_readfds, setsize);
  }

  if (writefds) {
    memcpy(writefds, &out_writefds, setsize);
  }

  if (exceptfds) {
    memset(exceptfds, 0, setsize);
  }
  return res;
}
