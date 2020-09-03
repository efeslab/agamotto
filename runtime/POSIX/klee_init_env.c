//===-- klee_init_env.c ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/klee.h"
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include "fd.h"
#include "misc.h"
#include "sockets.h"
#include "socksim/client.h"
#include "socksim/sockets_simulator.h"
#include "symfs.h"
#include "netlink.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

static void __emit_error(const char *msg) {
  klee_report_error(__FILE__, __LINE__, msg, "user.err");
}

/* Helper function that converts a string to an integer, and
   terminates the program with an error message is the string is not a
   proper number */   
static long int __str_to_int(char *s, const char *error_msg) {
  long int res = 0;
  char c;

  if (!*s) __emit_error(error_msg);

  while ((c = *s++)) {
    if (c == '\0') {
      break;
    } else if (c>='0' && c<='9') {
      res = res*10 + (c - '0');
    } else {
      __emit_error(error_msg);
    }
  }
  return res;
}

static int __isprint(const char c) {
  /* Assume ASCII */
  return (32 <= c && c <= 126);
}

static int __streq(const char *a, const char *b) {
  while (*a == *b) {
    if (!*a)
      return 1;
    a++;
    b++;
  }
  return 0;
}

static int __strlen(const char *s) {
  int i = 0;
  while (*s++) {
    i++;
  }
  return i;
}

static char *__get_sym_str(int numChars, char *name) {
  int i;
  char *s = malloc(numChars+1);
  klee_mark_global(s);
  klee_make_symbolic(s, numChars+1, name);

  for (i=0; i<numChars; i++)
    klee_posix_prefer_cex(s, __isprint(s[i]));
  
  s[numChars] = '\0';
  return s;
}

static void __add_arg(int *argc, char **argv, char *arg, int argcMax) {
  if (*argc==argcMax) {
    __emit_error("too many arguments for klee_init_env");
  } else {
    argv[*argc] = arg;
    (*argc)++;
  }
}

static char help_msg[] = 
"klee_init_env\n\n"
"usage: (klee_init_env) [options] [program arguments]\n"
"\n"
" General symfs stuff\n"
"  -sym-arg <N>               - Replace by a symbolic argument with length N\n"
"  -sym-args <MIN> <MAX> <N>  - Replace by at least MIN arguments and at most\n"
"                               MAX arguments, each with maximum length N\n"
"  -sym-files <NUM> <N>       - Make NUM symbolic files ('A', 'B', 'C', etc.),\n"
"                               each with size N. (type: PURE_SYMBOLIC)\n"
"  -sym-file <FILE>           - Make a symbolic file based on the name, stats, \n"
"                               size of the given <FILE>. (type: SYMBOLIC).\n"
"  -con-file <FILE>\n"
"  -con-file-max <FILE> <MAX> - Import a concrete file to the symbolic file \n"
"                               system. If <MAX> is provided, file can grow\n"
"                               in size to be larger than the original <FILE>,\n"
"                               up to <MAX> bytes total (type: CONCRETE)\n"
"\n"
" Persistent Memory stuff\n"
"  -sym-pmem <FILE> <N>      - Provide a symbolic persistent memory file and size.\n"
"  -sym-pmem-init-from <INIT_FILE_PATH>\n"
"                            - Initialize the persistent memory file to\n"
"                              concrete values based on values from a real file.\n"
"                              Also uses the name (base) and stat structure."
"  -sym-pmem-zeroed <FILE> <N>\n"
"                            - Initialize the persistent memory file to all zeroes \n"
"  -sym-pmem-delay <FILE> <N>\n"
"                            - This delays the creation of the pmem file at runtime\n"
"                              rather than on environment init. Essentially, this allows\n"
"                              us to create a new file which we know to mark symbolic.\n"
"                              Implies that the file will be created with zeroed memory.\n"
"\n"
" Sockets\n"
"  -tcp-client-text <NAME> <PORT> <TEXT>\n"
"                            - Creates a simulated TCP client that connects to\n"
"                              localhost:<PORT>, sends <TEXT>, then closes.\n"
"  -tcp-client-sym <NAME> <PORT> <N>\n"
"                            - Creates a simulated TCP client that connects to\n"
"                              localhost:<PORT>, sends a symbolic payload of\n"
"                              size <N>, then closes.\n"
"  -tcp-client-file <NAME> <PORT> <FILE>\n"
"                            - Creates a simulated TCP client that connects to\n"
"                              localhost:<PORT>, sends the data contained in\n"
"                              <FILE>, then closes.\n"
"  -sock-handler <NAME>      - Use predefined socket handler\n"
"  -sock-args <N> <PARAM...> - Can be supplied after -sock-handler\n"
"                              to provide <N> implementation-specific\n"
"                              configuration parameters <PARAM...>.\n"
"  -symbolic-sock-handler    - Inform socket handler that it is used during a\n"
"                              symbolic replay. (default=false)\n"
"\n"
" Other\n"
"  -sym-stdin <N>            - Make stdin symbolic with size N.\n"
"  -sym-file-stdin           - Make symbolic stdin behave like piped in from\n"
"                              a file if set.\n"
"  -sym-stdout               - Make stdout symbolic.\n"
"  -save-all-writes          - Allow write operations to execute as expected\n"
"                              even if they exceed the file size. If set to 0, all\n"
"                              writes exceeding the initial file size are discarded.\n"
"                              Note: file offset is always incremented.\n"
"  -max-fail <N>             - Allow up to N injected failures\n"
"  -unsafe                   - Allow non-RD_ONLY (unsafe) access to concrete \n"
"                              files.\n"
"  -no-overlapped            - Do not keep per-state concrete file offsets\n"
"  -fd-fail                  - Shortcut for '-max-fail 1'\n"
"  -posix-debug              - Enable debug message in POSIX runtime\n";

static void __add_symfs_file(fs_init_descriptor_t *fid,
                             enum sym_file_type file_type,
                             enum sym_pmem_file_type pmem_type,
                             const char *file_path,
                             long file_size) {
  if (fid->n_sym_files >= MAX_FILES) {
    __emit_error("Maximum number of allowed symbolic files exceeded when "
                 "adding SYMBOLIC/CONCRETE files");
  }

  sym_file_descriptor_t *sfd = &fid->sym_files[fid->n_sym_files++];
  sfd->file_type = file_type;
  sfd->pmem_type = pmem_type;
  sfd->file_path = file_path;
  sfd->file_size = file_size;
}

static int __parse_optional_sock_handler_args(int argc, char **argv, int k,
                                              socket_event_handler_t *handler) {
  if (__streq(argv[k], "--sock-args") ||
      __streq(argv[k], "-sock-args")) {
    const char *msg = "--sock-args expects a non-zero integer argument <argc> "
      "followed by <argc> string arguments";

    int shargc = 0;
    char **shargv = NULL;
    
    if (++k >= argc) {
      __emit_error(msg);
    }

    shargc = __str_to_int(argv[k++], msg);
    shargv = malloc(shargc * sizeof(char *));
    int s= 0;
    while (s < shargc) {
      if (k >= argc) {
        __emit_error(msg);
      }
      shargv[s++] = argv[k++];
    }

    handler->argc = shargc;
    handler->argv = (const char **)shargv;
  }

  return k;
}

static void __add_socket_handler(socksim_init_descriptor_t *ssid,
                                 socket_event_handler_t *handler) {
  if (ssid->n_handlers >= MAX_SOCK_EVT_HANDLE) {
    __emit_error("Maximum number of custom socket handlers reached.");
  }

  ssid->handlers[ssid->n_handlers++] = handler;
}

void klee_init_env(int *argcPtr, char ***argvPtr) {
  int argc = *argcPtr;
  char **argv = *argvPtr;

  int new_argc = 0;
  char *new_argv[1024];
  unsigned max_len, min_argvs, max_argvs;
  int save_all_writes_flag = 0;
  char **final_argv;
  char sym_arg_name[6] = "arg";
  unsigned sym_arg_num = 0;
  int k = 0, i;
  fs_init_descriptor_t fid;
  fid.n_sym_files = 0;
  fid.sym_stdin_len = 0;
  fid.allow_unsafe = 0;
  fid.overlapped_writes = 1;
  fid.sym_file_stdin_flag = 0;
  fid.sym_stdout_flag = 0;
  fid.max_failures = 0;
  // This is defined in common.c
  enableDebug = 0;

  socksim_init_descriptor_t ssid;
  ssid.n_handlers = 0;
  // This is defined in sockets_simulator.c
  useSymbolicHandler = 0;

  sym_arg_name[5] = '\0';

  // Recognize --help when it is the sole argument.
  if (argc == 2 && __streq(argv[1], "--help")) {
    __emit_error(help_msg);
  }

  while (k < argc) {
    if (__streq(argv[k], "--sym-arg") || __streq(argv[k], "-sym-arg")) {
      const char *msg = "--sym-arg expects an integer argument <max-len>";
      if (++k == argc)
        __emit_error(msg);

      max_len = __str_to_int(argv[k++], msg);

      if (sym_arg_num > 99)
        __emit_error("No more than 100 symbolic arguments allowed.");

      sym_arg_name[3] = '0' + sym_arg_num / 10;
      sym_arg_name[4] = '0' + sym_arg_num % 10;
      sym_arg_num++;
      __add_arg(&new_argc, new_argv, __get_sym_str(max_len, sym_arg_name),
                1024);
    } else if (__streq(argv[k], "--sym-args") ||
               __streq(argv[k], "-sym-args")) {
      const char *msg = "--sym-args expects three integer arguments "
                        "<min-argvs> <max-argvs> <max-len>";

      if (k + 3 >= argc)
        __emit_error(msg);

      k++;
      min_argvs = __str_to_int(argv[k++], msg);
      max_argvs = __str_to_int(argv[k++], msg);
      max_len = __str_to_int(argv[k++], msg);

      if ((min_argvs > max_argvs) || (min_argvs == 0 && max_argvs == 0))
        __emit_error("Invalid range to --sym-args");

      int n_args = klee_range(min_argvs, max_argvs + 1, "n_args");

      if (sym_arg_num + max_argvs > 99)
        __emit_error("No more than 100 symbolic arguments allowed.");
        
      
      // Instead of using a symboclic number of args, 
      // we set the number of symbolic args as the max-argvs
      for (i = 0; i < n_args; i++) {
        sym_arg_name[3] = '0' + sym_arg_num / 10;
        sym_arg_name[4] = '0' + sym_arg_num % 10;
        sym_arg_num++;
        
        __add_arg(&new_argc, new_argv, __get_sym_str(max_len, sym_arg_name),
                  1024);
      }
    } else if (__streq(argv[k], "--sym-files") ||
               __streq(argv[k], "-sym-files")) {
      const char *msg = "--sym-files expects two integer arguments "
                        "<no-sym-files> <sym-file-len>";

      if (k + 2 >= argc)
        __emit_error(msg);

      if (fid.n_sym_files != 0)
        __emit_error("Multiple --sym-files are not allowed.\n");

      k++;
      long int sym_files = __str_to_int(argv[k++], msg);
      long int sym_file_len = __str_to_int(argv[k++], msg);

      if (sym_files == 0)
        __emit_error("The first argument to --sym-files (number of files) "
                     "cannot be 0\n");

      if (sym_file_len == 0)
        __emit_error("The second argument to --sym-files (file size) "
                     "cannot be 0\n");
      int i;
      for (i=0; i < sym_files; ++i) {
        if (fid.n_sym_files >= MAX_FILES) {
          __emit_error("Maximum number of allowed symbolic files exceeded when "
                       "adding pure symbolic files");
        }
        sym_file_descriptor_t *sfd = &fid.sym_files[fid.n_sym_files++];
        sfd->file_type = PURE_SYMBOLIC;
        sfd->file_size = sym_file_len;
      }
    } else if (__streq(argv[k], "--sym-file") ||
               __streq(argv[k], "-sym-file")) {
      const char *msg = "--sym-file expect one argument "
                        "<backend-file-path>";

      if (++k >= argc)
        __emit_error(msg);

      const char *file_path = argv[k++];
      __add_symfs_file(&fid, SYMBOLIC, NOT_PMEM, file_path, 0);
    } else if (__streq(argv[k], "--con-file") ||
               __streq(argv[k], "-con-file") ||
               __streq(argv[k], "--con-file-max") ||
               __streq(argv[k], "-con-file-max")) {
      int has_maxlen = __strlen(argv[k]) > __strlen("--con-file");

      const char *msg;
      if (has_maxlen) {
        msg = "--con-file-max expects one string argument <backend-file-path> "
              "and one integer argument <max-size>";
      } else {
        msg = "--con-file expects one string argument <backend-file-path>";
      }

      if (k + 1 + has_maxlen >= argc)
        __emit_error(msg);
      k++;

      const char *file_path;
      long maxlen = 0;

      file_path = argv[k++];
      if (has_maxlen) {
        maxlen = __str_to_int(argv[k++], msg);
      }
      __add_symfs_file(&fid, CONCRETE, NOT_PMEM, file_path, maxlen);
    } else if (__streq(argv[k], "--sym-pmem") || __streq(argv[k], "-sym-pmem")) {
      const char *msg = "--sym-pmem expects one string argument "
                "<sym-pmem-filename> and one integer argument <sym-pmem-size>";
      if (k + 2 >= argc) __emit_error(msg);
      k++;
      const char *file_name = argv[k++];
      long file_size = __str_to_int(argv[k++], msg);
      if (!file_size) {
        __emit_error("The second argument to --sym-pmem (file size) cannot be 0\n");
      }
      __add_symfs_file(&fid, SYMBOLIC, PMEM_SYMBOLIC, file_name, file_size);
    } else if (__streq(argv[k], "--sym-pmem-init-from") || 
               __streq(argv[k], "-sym-pmem-init-from")) {
      const char *msg = "--sym-pmem-init-from expects one string <init_from_path>";
      if (k + 1 >= argc)
        __emit_error(msg);
      k++;
      const char *file_path = argv[k++];
      __add_symfs_file(&fid, SYMBOLIC, PMEM_FROM_CONCRETE, file_path, 0);
    } else if (__streq(argv[k], "--sym-pmem-zeroed") || 
               __streq(argv[k], "-sym-pmem-zeroed")) {
      const char *msg = "--sym-pmem-zeroed expects one string argument "
                        "<filename> and one integer argument <size>";
      if (k + 2 >= argc) __emit_error(msg);
      k++;
      const char *file_name = argv[k++];
      long file_size = __str_to_int(argv[k++], msg);
      if (!file_size) {
        __emit_error("--sym-pmem-zeroed file size cannot be 0\n");
      }
      __add_symfs_file(&fid, SYMBOLIC, PMEM_SYM_ZERO, file_name, file_size);
    } else if (__streq(argv[k], "--sym-pmem-delay") || 
               __streq(argv[k], "-sym-pmem-delay")) {
      const char *msg = "--sym-pmem-delay expects one string argument "
                        "<filename> and one integer argument <size>";
      if (k + 2 >= argc) __emit_error(msg);
      k++;
      const char *file_name = argv[k++];
      long file_size = __str_to_int(argv[k++], msg);
      if (!file_size) {
        __emit_error("--sym-pmem-delay file size cannot be 0\n");
      }
      printf("%s: adding delay-create pmem file!\n", __func__);
      __add_symfs_file(&fid, SYMBOLIC, PMEM_DELAY_CREATE, file_name, file_size);
    } else if (__streq(argv[k], "--tcp-client-text") ||
               __streq(argv[k], "-tcp-client-text")) {
      const char *msg = "--tcp-client-text expects three arguments: "
                "<NAME> <PORT> <TEXT>";
      if (k + 3 >= argc) __emit_error(msg);
      k++;
      socket_event_handler_t *handler;
      const char *name = argv[k++];
      int port = (int)__str_to_int(argv[k++], msg);
      const char *text = argv[k++];
      handler = create_simple_text_client(name, port, text, __strlen(text));
      __add_socket_handler(&ssid, handler);
    } else if (__streq(argv[k], "--tcp-client-sym") ||
               __streq(argv[k], "-tcp-client-sym")) {
      const char *msg = "--tcp-client-sym expects three arguments: "
                "<NAME> <PORT> <N>";
      if (k + 3 >= argc) __emit_error(msg);
      k++;
      socket_event_handler_t *handler;
      const char *name = argv[k++];
      int port = (int)__str_to_int(argv[k++], msg);
      long size = __str_to_int(argv[k++], msg);
      char *text = malloc(size);
      klee_make_symbolic(text, size, name);
      handler = create_simple_text_client(name, port, text, size);
      __add_socket_handler(&ssid, handler);
    } else if (__streq(argv[k], "--sock-handler") ||
               __streq(argv[k], "-sock-handler")) {
      const char *msg = "--sock-handler expects one string <NAME>";
      if (++k >= argc)
        __emit_error(msg);
      const char *name = argv[k++];
      socket_event_handler_t *handler = get_predefined_socket_handler(name);
      if (handler == NULL)
        __emit_error("Unknown socket handler");
      k = __parse_optional_sock_handler_args(argc, argv, k, handler);
      __add_socket_handler(&ssid, handler);
    } else if (__streq(argv[k], "--tcp-client-file") ||
               __streq(argv[k], "-tcp-client-file")) {
      const char *msg = "--tcp-client-file expects three arguments: "
                "<NAME> <PORT> <FILE>";
      if (k + 3 >= argc) __emit_error(msg);
      k++;
      socket_event_handler_t *handler;
      const char *name = argv[k++];
      int port = (int)__str_to_int(argv[k++], msg);
      const char *path = argv[k++];
      handler = create_client_from_file(name, port, path);
      __add_socket_handler(&ssid, handler);
    } else if (__streq(argv[k], "--symbolic-sock-handler") ||
               __streq(argv[k], "-symbolic-sock-handler")) {
      k++;
      useSymbolicHandler = 1;
    } else if (__streq(argv[k], "--sym-stdin") ||
               __streq(argv[k], "-sym-stdin")) {
      const char *msg =
          "--sym-stdin expects one integer argument <sym-stdin-len>";

      if (++k == argc)
        __emit_error(msg);

      fid.sym_stdin_len = __str_to_int(argv[k++], msg);
    } else if (__streq(argv[k], "--sym-file-stdin") ||
               __streq(argv[k], "-sym-file-stdin")) {
      fid.sym_file_stdin_flag = 1;
      k++;
    } else if (__streq(argv[k], "--sym-stdout") ||
               __streq(argv[k], "-sym-stdout")) {
      fid.sym_stdout_flag = 1;
      k++;
    } else if (__streq(argv[k], "--save-all-writes") ||
               __streq(argv[k], "-save-all-writes")) {
      save_all_writes_flag = 1;
      k++;
    } else if (__streq(argv[k], "--unsafe") ||
               __streq(argv[k], "-unsafe")) {
      fid.allow_unsafe = 1;
      k++;
    } else if (__streq(argv[k], "--no-overlapped") ||
               __streq(argv[k], "-no-overlapped")) {
      fid.overlapped_writes = 0;
      k++;
    } else if (__streq(argv[k], "--fd-fail") || __streq(argv[k], "-fd-fail")) {
      fid.max_failures = 1;
      k++;
    } else if (__streq(argv[k], "--bout-file") || __streq(argv[k], "-bout-file")) {
      k += 2;
    } else if (__streq(argv[k], "--max-fail") ||
               __streq(argv[k], "-max-fail")) {
      const char *msg = "--max-fail expects an integer argument <max-failures>";
      if (++k == argc)
        __emit_error(msg);

      fid.max_failures = __str_to_int(argv[k++], msg);
    } else if (__streq(argv[k], "--posix-debug") ||
               __streq(argv[k], "-posix-debug")) {
      k++;
      enableDebug = 1;
    } else {
      /* simply copy arguments */
      __add_arg(&new_argc, new_argv, argv[k++], 1024);
    }
  }

  posix_debug_msg("%s: setting up POSIX env...\n", __func__);

  if (!fid.sym_stdin_len && fid.sym_file_stdin_flag) {
    __emit_error("--sym-file-stdin shouldn't be set along without --sym-stdin");
  }
  final_argv = (char **)malloc((new_argc + 1) * sizeof(*final_argv));
  klee_mark_global(final_argv);
  memcpy(final_argv, new_argv, new_argc * sizeof(*final_argv));
  final_argv[new_argc] = 0;
  
  *argcPtr = new_argc;
  *argvPtr = final_argv;

  klee_init_sym_env(save_all_writes_flag);
  klee_init_symfs(&fid);
  // (iangneal): we have our own thanks
  // klee_init_mmap();
  klee_init_network();
  klee_init_netlink();
  klee_init_sockets_simulator(&ssid);
  klee_init_threads();
}

/* The following function represents the main function of the user application
 * and is renamed during POSIX setup */
int __klee_posix_wrapped_main(int argc, char **argv, char **envp);

// (bgreeves) a lovely hack to make sure library
// constructors can access POSIX functions if they want.
// KLEE will insert the klee.ctor_stub here instead of giving
// it to uclibc if POSIX runtime is enabled.
// (See KModule.cpp)
int __klee_posix_ctor_stub_insert_point(int argcPtr, char **argvPtr, char** envp) {
  // call to klee.ctor_stub() to be inserted here during module preparation
  return __klee_posix_wrapped_main(argcPtr, argvPtr, envp);
}

/* This wrapper gets called instead of main if POSIX setup is used
 * And it will be renamed to `__user_main` during POSIX setup
 */
int __klee_posix_wrapper(int argcPtr, char **argvPtr, char** envp) {
  klee_init_env(&argcPtr, &argvPtr);
  return __klee_posix_ctor_stub_insert_point(argcPtr, argvPtr, envp);
}
