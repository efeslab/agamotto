#include "memcached.h"

#include <stdio.h>

#define MEMCACHED_PORT 11211

static void memcached_1_5_13_client_func(void *);
static void memcached_update_client_func(void *);

memcached_handler_t
__memcached_1_5_13_handler = SIMULATED_CLIENT_HANDLER("memcached_1.5.13",
                                                      MEMCACHED_PORT,
                                                      memcached_1_5_13_client_func);
memcached_handler_t
__memcached_update_handler = SIMULATED_CLIENT_HANDLER("memcached_update",
                                                      MEMCACHED_PORT,
                                                      memcached_update_client_func);

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
  int nwrites = 1;
  char cmd[128];
  char payload[128];

  if (useSymbolicHandler) {
    klee_make_symbolic(payload, sizeof(payload), "memcached_update_payload");

    klee_make_symbolic(&nwrites, sizeof(nwrites), "memcached_update_nwrites");
    klee_assume(nwrites <= 10);
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
  }
  
  const char shutdown[] = "shutdown\r\n";
  ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  posix_debug_msg("payload (shutdown) write result %d\n", ret);
}

