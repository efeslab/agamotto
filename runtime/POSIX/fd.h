//===-- fd.h ---------------------------------------------------*- C++ -*--===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_FD_H
#define KLEE_FD_H

#include "klee/Config/config.h"
#include "config.h"

#ifndef _LARGEFILE64_SOURCE
#error "_LARGEFILE64_SOURCE should be defined"
#endif

#include <dirent.h>
#include <sys/types.h>

#ifdef HAVE_SYSSTATFS_H
#include <sys/statfs.h>
#endif

#ifdef __APPLE__
#include <sys/dtrace.h>
#endif
#ifdef __FreeBSD__
#include "FreeBSD.h"
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/mount.h>
#include <sys/param.h>
#if !defined(dirent64)
#define dirent64 dirent
#endif
#endif

#include <sys/uio.h>

typedef enum {
  eOpen         = (1 << 0), /* eOpen is set when this fd is valid*/
  eCloseOnExec  = (1 << 1),
  eIsFile       = (1 << 4),
  eIsPIPE       = (1 << 5),
  eIsSocket     = (1 << 6)
} exe_file_flag_t;

typedef struct {
  unsigned int refcount;
  unsigned int queued;
  int flags;      /* O_RDONLY, O_WRONLY, O_RDWR, O_CLOEXEC, etc... */
} file_base_t;

/* model a linux file descriptor (could be a regular file, pipe, socket, etc.)
 * If the `dfile` field is a nullptr, then this is a concrete file descriptor.
 * Otherwise, a symbolic file descriptor
 */
typedef struct {
  unsigned int attr;      /* flags from exec_file_flag_t */
  file_base_t *io_object; /* This is dynamically allocated
                           * type: file_t*, if fd is a regular file;
                           * TODO pipe type;
                           * TODO socket type;
                           */
} fd_entry_t;
// NOTE: exe_file_t -> fd_entry_t

/* Note, if you change this structure be sure to update the
   initialization code if necessary. New fields should almost
   certainly be at the end. */
typedef struct {
  fd_entry_t fds[MAX_FDS];
  mode_t umask; /* process umask */
  unsigned version;
  /* If set, writes execute as expected (but writes exceeding a symbolic file
     size will abort).  Otherwise, writes extending the file size only change
     the contents up to the initial size. The file offset is always incremented
     correctly. */
  int save_all_writes; 
} exe_sym_env_t;

extern exe_sym_env_t __exe_env;

void klee_init_env(int *argcPtr, char ***argvPtr);

/* *** */
struct stat64;
int __fd_allocate();
void __fd_clear(int fd);
fd_entry_t *__get_fd(int fd);

int __fd_fstat(int fd, struct stat64 *buf);
int __fd_ftruncate(int fd, off64_t length);
int __fd_statfs(const char *path, struct statfs *buf);

/* *** */
#define _FD_SET(n, p)    ((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
#define _FD_CLR(n, p)    ((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
#define _FD_ISSET(n, p)  ((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
#define _FD_ZERO(p)  memset((char *)(p), '\0', sizeof(*(p)))

ssize_t _scatter_read(int fd, const struct iovec *iov, int iovcnt);
ssize_t _gather_write(int fd, const struct iovec *iov, int iovcnt);

/* INIT */
void klee_init_sym_env(int save_all_writes);

#endif /* KLEE_FD_H */
