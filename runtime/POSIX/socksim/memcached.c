#include "memcached.h"

#include <stdio.h>

#define MEMCACHED_PORT 11211

static void memcached_1_5_13_client_func(void *);
static void memcached_update_client_func(void *);
static void memcached_rand_client_func(void *);
static void memcached_file_client_func(void *);

memcached_handler_t
__memcached_1_5_13_handler = SIMULATED_CLIENT_HANDLER("memcached_1.5.13",
                                                      MEMCACHED_PORT,
                                                      memcached_1_5_13_client_func);
memcached_handler_t
__memcached_update_handler = SIMULATED_CLIENT_HANDLER("memcached_update",
                                                      MEMCACHED_PORT,
                                                      memcached_update_client_func);

memcached_handler_t
__memcached_rand_handler   = SIMULATED_CLIENT_HANDLER("memcached_rand",
                                                      MEMCACHED_PORT,
                                                      memcached_rand_client_func);

memcached_handler_t
__memcached_file_handler   = SIMULATED_CLIENT_HANDLER("memcached_file",
                                                      MEMCACHED_PORT,
                                                      memcached_file_client_func);

/**
 * utilities
 */

static void memcached_send(simulated_client_handler_t *self_hdl, char *req) {
  static const char flushSeq[] = "\r\n";
  char payload[2048];

  memset(payload, 0, sizeof(payload));
  
  int ret;

  snprintf(payload, sizeof(payload), "%s%s", req, flushSeq);

  posix_debug_msg("[MEMCACHED FILE] SENDING '%s'", payload);

  ret = _write_socket(self_hdl->client_sock, payload, strlen(payload));

  posix_debug_msg("[MEMCACHED FILE] SENDING returned %d (want %lu)", ret, strlen(payload));

  klee_assert(ret == strlen(payload) && "nope!");
}

static void memcached_recv(simulated_client_handler_t *self_hdl, char *req, size_t len) {
  int ret;

  ret = _read_socket(self_hdl->client_sock, req, len - 1);

  posix_debug_msg("[MEMCACHED FILE] RECV '%s'", req);
  posix_debug_msg("[MEMCACHED FILE] RECV returned %d (want %lu)", ret, strlen(req));

  klee_assert(ret == strlen(req) && "nope!");
}

/**
 * memcached file handler
 */
#define EMITERR(msg) klee_report_error(__FILE__, __LINE__, msg, "user.err")

#define ATOI(arg, msg) ({ \
  int __i = atoi((arg)); \
  if (__i == 0) \
    EMITERR(msg); \
  __i; \
})

static void memcached_file_argparse(void *self,
                                    const char **file_out) {
  socket_event_handler_t *base = (socket_event_handler_t *)self;
  int argc = base->argc;
  const char **argv = base->argv;
  const char *msg = "redis_file socket handler expects a string argument <request-file>";

  if (argc == 0 || argc > 1) {
    EMITERR(msg);
  }

  *file_out = argv[0];
}

static void newline_check(char *req) {
  posix_debug_msg("[MEMCACHED FILE] %s '%s'", __FUNCTION__, req);
  char *newline = strchr(req, '\n');
  if (!newline) {
    newline = strchr(req, '\r');
    klee_assert(newline != NULL);
    *newline = 0;
  } else {
    if (newline[-1] == '\r') {
      newline[-1] = 0;
    }
    // Exclude \r\n, send_redis_req will append it
    *newline = 0;
  }

  klee_assert(strchr(req, '\n') == NULL);
  klee_assert(strchr(req, '\r') == NULL);
}

static void memcached_file_client_func(void *self) {
  simulated_client_handler_t *client = (simulated_client_handler_t *)self;
  posix_debug_msg("[MEMCACHED FILE] INIT\n");

  const char *fname = NULL;
  memcached_file_argparse(self, &fname);
  posix_debug_msg("[MEMCACHED FILE] FILE IS '%s'\n", fname);

  int ret = 0;
  FILE *file = fopen(fname, "r");
  char req[2048];
  bool is_shutdown = false;
  while (fgets(req, sizeof(req), file)) {

    newline_check(req);

    posix_debug_msg("[MEMCACHED FILE] '%s'\n", req);

    memcached_send(client, req);
    
    if (!strncmp(req, "set", 3)) {
      // Need to send the next line as well
      if (!fgets(req, sizeof(req), file)) assert(0 && "bad file! no value to send");
      newline_check(req);
      posix_debug_msg("[MEMCACHED FILE] SEND VALUE '%s'\n", req);
      memcached_send(client, req);
    }

    if (!strncmp(req, "shutdown", 8)) {
      is_shutdown = true;
    }

    memset(req, 0, 2048);
    memcached_recv(client, req, 2048);
    posix_debug_msg("[MEMCACHED FILE] RECV '%s'\n", req);
  }

  if (!is_shutdown) memcached_send(client, "shutdown");

  fclose(file);
  posix_debug_msg("[MEMCACHED FILE] DONE\n");
}

/**
 * memcached 1.5.13 handler
 */

static void memcached_1_5_13_client_func(void *self) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;

  // char payload[1024];
  // const char val[] = "the_value";
  // snprintf(payload, sizeof(payload), "set the_key 1 0 %lu\n", sizeof(val));
  char payload[] = "set the_key 1 0 1\r\na\r\nshutdown\r\n";
  if (useSymbolicHandler) {
    klee_make_symbolic(payload, sizeof(payload), "memcached_1_5_13_payload");
    strncpy(payload, "set", 3);
    strncpy(payload + 22, "shutdown", 8);
  }
  int ret = _write_socket(self_hdl->client_sock, payload, sizeof(payload) - 1);
  posix_debug_msg("payload (set) write result %d\n", ret);

  // sleep(30);

  // snprintf(payload, sizeof(payload), "%s\n", val);
  // ret = _write_socket(self_hdl->client_sock, payload, sizeof(payload));
  // posix_debug_msg("payload (val) write result %d\n", ret);

  // snprintf(payload, sizeof(payload), "shutdown\r\n");
  // const char shutdown[] = "shutdown\r\n";
  // ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  // posix_debug_msg("payload (shutdown) write result %d\n", ret);
}

/**
 * memcached update handler
 * 
 * This handler will issue symbolic set commands. After a static number of 
 * updates are sent, the update handler will issue a shutdown command.
 * 
 * If --symbolic-sock-handler is set, then the handler will decide the number 
 * of updates symbolically.
 */

static void memcached_update_client_func(void *self) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;

  static const char flushSeq[] = "\r\n";
  int nwrites = 10;
  char cmd[128];
  char payload[128];
  char resp[128];
  memset(resp, 0, sizeof(resp));

  if (useSymbolicHandler) {
    // klee_make_symbolic(payload, sizeof(payload), "memcached_update_payload");
    klee_make_symbolic(&nwrites, sizeof(nwrites), "memcached_update_nwrites");
    klee_assume(nwrites <= 10000);
  } 

  memset(payload, 0, sizeof(payload));

  int i, ret;
  for (i = 0; i < nwrites; i++) {
    snprintf(payload, sizeof(payload), "val_%d%s", i, flushSeq);
    snprintf(cmd, sizeof(cmd), "set key_%d 1 0 %lu%s",
            i, strlen(payload) - strlen(flushSeq), flushSeq);

    ret = _write_socket(self_hdl->client_sock, cmd, strlen(cmd));
    posix_debug_msg("CMD: '%s', ret %d\n", cmd, ret);
    ret = _write_socket(self_hdl->client_sock, payload, strlen(payload));
    posix_debug_msg("VAL: '%s', ret %d\n", payload, ret);
    posix_debug_msg("Write #%d: payload (set) write result %d\n", i, ret);

    ret = _read_socket(self_hdl->client_sock, resp, sizeof(resp) - 1);
    if (strcmp(resp, "STORED\r\n")) {
      posix_debug_msg("'%s' != '%s'\n", resp, "STORED\r\n");
      klee_error("KV pair not stored!!");
    }
  }
  
  const char shutdown[] = "shutdown\r\n";
  ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  posix_debug_msg("payload (shutdown) write result %d\n", ret);
}

static void do_write(simulated_client_handler_t *self_hdl, int i) {
  static const char flushSeq[] = "\r\n";
  char cmd[128];
  char payload[128];
  char resp[128];

  memset(cmd, 0, sizeof(cmd));
  memset(payload, 0, sizeof(payload));
  memset(resp, 0, sizeof(resp));
  
  int expire = 0;
  klee_make_symbolic(&expire, sizeof(expire), "expire");
  klee_assume(expire >= 0);
  klee_assume(expire <= 30);

  int ret;

  snprintf(payload, sizeof(payload), "val_%d%s", i, flushSeq);
  snprintf(cmd, sizeof(cmd), "set key_%d 1 %d %lu%s",
          i, expire, strlen(payload) - strlen(flushSeq), flushSeq);

  ret = _write_socket(self_hdl->client_sock, cmd, strlen(cmd));
  posix_debug_msg("CMD: '%s', ret %d\n", cmd, ret);
  ret = _write_socket(self_hdl->client_sock, payload, strlen(payload));
  posix_debug_msg("VAL: '%s', ret %d\n", payload, ret);
  posix_debug_msg("Write #%d: payload (set) write result %d\n", i, ret);

  ret = _read_socket(self_hdl->client_sock, resp, sizeof(resp) - 1);
  if (strcmp(resp, "STORED\r\n")) {
    posix_debug_msg("'%s' != '%s'\n", resp, "STORED\r\n");
    klee_error("KV pair not stored!!");
  }
}

static void do_read(simulated_client_handler_t *self_hdl, int i) {
  static const char flushSeq[] = "\r\n";
  char cmd[128];
  char resp[128];
  int ret;
  
  snprintf(cmd, sizeof(cmd), "get key_%d%s", i, flushSeq);

  ret = _write_socket(self_hdl->client_sock, cmd, strlen(cmd));
  posix_debug_msg("CMD: '%s', ret %d\n", cmd, ret);

  ret = _read_socket(self_hdl->client_sock, resp, sizeof(resp) - 1);
  klee_assert(ret > 0);
}

static void memcached_rand_client_func(void *self) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;

  int nwrites = 10;

  bool doWrite = true;
  klee_make_symbolic(&nwrites, sizeof(nwrites), "memcached_update_nwrites");
  klee_assume(nwrites <= 1000000);

  klee_make_symbolic(&doWrite, sizeof(doWrite), "memcached_choice");

  int i, ret;
  for (i = 0; i < nwrites; i++) {
    if (doWrite) {
      do_write(self_hdl, i);
    } else {
      do_read(self_hdl, i);
    }
  }
  
  const char shutdown[] = "shutdown\r\n";
  ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  posix_debug_msg("payload (shutdown) write result %d\n", ret);
}