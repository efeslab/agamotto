#include "memcached.h"

#include <stdio.h>

#define MEMCACHED_PORT 11211

static void memcached_1_5_13_client_func(void *);
static void memcached_update_client_func(void *);
static void memcached_rand_client_func(void *);

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