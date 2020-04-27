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

#ifndef COMMON_H_
#define COMMON_H_

#include "config.h"

#include <sys/types.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
// IOCtl Codes
////////////////////////////////////////////////////////////////////////////////

#define KLEE_SIO_SYMREADS   0xfff00     // Enable symbolic reads for a socket
#define KLEE_SIO_READCAP    0xfff01     // Set a maximum cap for reading from the stream

////////////////////////////////////////////////////////////////////////////////
// Klee Event Codes
////////////////////////////////////////////////////////////////////////////////

#define __KLEE_EVENT_BREAKPOINT   10
#define __KLEE_EVENT_PKT_FRAGMENT   1337

////////////////////////////////////////////////////////////////////////////////
// Klee Breakpoint Types
////////////////////////////////////////////////////////////////////////////////

#define __KLEE_BREAK_TRACE  42

////////////////////////////////////////////////////////////////////////////////
// Klee Fork Types
////////////////////////////////////////////////////////////////////////////////

#define __KLEE_FORK_DEFAULT       0
#define __KLEE_FORK_FAULTINJ      1
#define __KLEE_FORK_SCHEDULE      2
#define __KLEE_FORK_INTERNAL      3
#define __KLEE_FORK_MULTI         4
#define __KLEE_FORK_TIMEOUT       5
#define __KLEE_FORK_FUZZ          6

static inline uint64_t get_fuzz_reason(int offset, int entropy) {
  return ((uint64_t)__KLEE_FORK_FUZZ & 0xFF) |
      (((uint64_t)entropy & 0xFF) << 8) |
      (((uint64_t)offset & 0xFFFF) << 16);
}


////////////////////////////////////////////////////////////////////////////////
// Model Infrastructure
////////////////////////////////////////////////////////////////////////////////

// A model needs to be declared only when it's supposed to interface
// an existing C library call.

#define __DECLARE_MODEL(type, name, ...) \
  type name(__VA_ARGS__);

#ifdef __FORCE_USE_MODELS
#define DECLARE_MODEL(type, name, ...) \
  __DECLARE_MODEL(type, name, ##__VA_ARGS__) \
  __attribute__((used)) static const void* __usage_model_ ## name = (void*)&__klee_model_ ## name;
#else
#define DECLARE_MODEL(type, name, ...) \
  __DECLARE_MODEL(type, name, ##__VA_ARGS__)
#endif

#define FORCE_IMPORT(name) \
  __attribute__((used)) static const void* __usage_ ## name = (void*) &name;

#define CALL_UNDERLYING(name, ...) \
    syscall(__NR_##name, __VA_ARGS__)

#define CALL_MODEL(name, ...) \
    name(__VA_ARGS__);

#define DEFINE_MODEL(type, name, ...) \
    type name(__VA_ARGS__)


#ifdef HAVE_FAULT_INJECTION

#define INJECT_FAULT(name, ...) \
    __inject_fault(#name, ##__VA_ARGS__)

#else

#define INJECT_FAULT(name, ...)     0

#endif

int klee_get_errno(void);

void *__concretize_ptr(const void *p);
size_t __concretize_size(size_t s);
off_t __concretize_offset(off_t o);
const char *__concretize_string(const char *s);

////////////////////////////////////////////////////////////////////////////////
// Static Lists
////////////////////////////////////////////////////////////////////////////////

#define STATIC_LIST_INIT(list)  \
  do { memset(&list, 0, sizeof(list)); } while (0)

#define STATIC_LIST_ALLOC(list, item) \
  do { \
    item = sizeof(list)/sizeof(list[0]); \
    unsigned int __i; \
    for (__i = 0; __i < sizeof(list)/sizeof(list[0]); __i++) { \
      if (!list[__i].allocated) { \
        list[__i].allocated = 1; \
        item = __i;  break; \
      } \
    } \
  } while (0)

#define STATIC_LIST_CLEAR(list, item) \
  do { memset(&list[item], 0, sizeof(list[item])); } while (0)

#define STATIC_LIST_CHECK(list, item) \
  (((item) < sizeof(list)/sizeof(list[0])) && (list[item].allocated))

////////////////////////////////////////////////////////////////////////////////
// Double Linked Lists (Linux kernel style)
////////////////////////////////////////////////////////////////////////////////
struct list_head {
  struct list_head *next, *prev;
};

#define list_entry(ptr, type, member) \
  ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

#define list_for_each_entry(pos, head, member)                    \
  for (pos = list_entry((head)->next, typeof(*pos), member);      \
       &pos->member != (head);                                    \
       pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_del(l_prev, l_next) \
  do { \
    (l_next)->prev = (struct list_head *)l_prev; \
    (l_prev)->next = (struct list_head *)l_next;  \
  } while(0)

#define list_del_init(entry)      \
  do {            \
    list_del(((struct list_head *)entry)->prev, \
      ((struct list_head *)entry)->next); \
    INIT_LIST_HEAD(entry);      \
  } while(0)

#define INIT_LIST_HEAD(list) \
  do { \
    ((struct list_head *)list)->next = (list); \
    ((struct list_head *)list)->prev = (list); \
  } while(0)

////////////////////////////////////////////////////////////////////////////////
// IOvec manipulation
////////////////////////////////////////////////////////////////////////////////

struct iovec;
size_t _count_iovec(const struct iovec *iov, int iovcnt);


////////////////////////////////////////////////////////////////////////////////
// Basic Types
////////////////////////////////////////////////////////////////////////////////
typedef uint64_t wlist_id_t;

////////////////////////////////////////////////////////////////////////////////
// Debugging message
////////////////////////////////////////////////////////////////////////////////
// This flag is configurable in klee_init_env (option "-posix-debug")
extern char enableDebug;
void posix_debug_msg(const char *fmt, ...);
////////////////////////////////////////////////////////////////////////////////
// Bidirectional string mapping helper: const char * <-> customizable types
////////////////////////////////////////////////////////////////////////////////
#define BEGIN_BISTRMAPPING(type, name)                                         \
  typedef struct {                                                             \
    const char *str;                                                           \
    type x;                                                                    \
  } STRMAP_##name##_t;                                                         \
  STRMAP_##name##_t MAPPING_##name[] = {
#define BISTRMAPPING_ENTRY(str,x) {str, x},
#define END_BISTRMAPPING(type, name, notfound)                                 \
  };                                                                           \
  const char *name##_to_str(type x) {                                          \
    int i;                                                                     \
    for (i = 0; i < sizeof(MAPPING_##name) / sizeof(STRMAP_##name##_t); ++i) { \
      if (x == MAPPING_##name[i].x) {                                          \
        return MAPPING_##name[i].str;                                          \
      }                                                                        \
    }                                                                          \
    return NULL;                                                               \
  }                                                                            \
  type name##_from_str(const char *str) {                                      \
    int i;                                                                     \
    for (i = 0; i < sizeof(MAPPING_##name) / sizeof(STRMAP_##name##_t); ++i) { \
      if (strcmp(str, MAPPING_##name[i].str) == 0) {                           \
        return MAPPING_##name[i].x;                                            \
      }                                                                        \
    }                                                                          \
    return notfound;                                                           \
  }

#endif /* COMMON_H_ */
