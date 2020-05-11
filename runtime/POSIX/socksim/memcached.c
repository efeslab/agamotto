#include "sockets_simulator.h"

#include <assert.h>
#include <stdio.h>
#include <pthread.h>

#define MEMCACHED_PORT 11211

typedef struct {
  socket_event_handler_t __base;
  socket_t *client_sock;
  socket_t *server_sock;
} memcached_1_5_13_handler_t;
static memcached_1_5_13_handler_t memcached_1_5_13_handler;

typedef struct {
  socket_event_handler_t __base;
  socket_t *client_sock;
  socket_t *server_sock;
} memcached_update_handler_t;
static memcached_update_handler_t memcached_update_handler;

// External references for sockets_simulator.c

const socket_event_handler_t *const
__memcached_1_5_13_handler = (const socket_event_handler_t *)&memcached_1_5_13_handler;

const socket_event_handler_t *const
__memcached_update_handler = (const socket_event_handler_t *)&memcached_update_handler;

/**
 * memcached 1.5.13 handler
 */

static void memcached_1_5_13_handler_init(void *self) {
  memcached_1_5_13_handler_t *self_hdl = (memcached_1_5_13_handler_t*)self;
  self_hdl->client_sock = _create_socket(AF_INET, SOCK_STREAM, 0);
};

static void
memcached_1_5_13_handler_post_bind(void *self, socket_t *sock,
                                          const struct sockaddr *addr,
                                          socklen_t addrlen) {
  memcached_1_5_13_handler_t *self_hdl = (memcached_1_5_13_handler_t*)self;
  // I am only interested in socket bind to DEFAULT_ADDR:MEMCACHED_PORT
  if (sock->domain != AF_INET) {
    posix_debug_msg("memcached_1_5_13 bind handler: no bind, not AF_INET\n");
    return;
  }
  const struct sockaddr_in *inetaddr = (struct sockaddr_in*)addr;
  if (inetaddr->sin_addr.s_addr != __net.net_addr.s_addr) {
    posix_debug_msg("memcached_1_5_13 bind handler: no bind, bad saddr \n");
    return;
  }
  if (inetaddr->sin_port != htons(MEMCACHED_PORT)) {
    posix_debug_msg("memcached_1_5_13 bind handler: no bind, bad port \n");
    return;
  }
  self_hdl->server_sock = sock;
  posix_debug_msg("memcached_1_5_13 bind handler catch the server socket\n");
}

typedef struct {
  memcached_1_5_13_handler_t *self_hdl;
} memcached_1_5_13_handler_post_listen_arg_t;

memcached_1_5_13_handler_post_listen_arg_t memcached_1_5_13_listen_arg;

static void *memcached_1_5_13_handler_post_listen_newthread(void *_arg) {
  posix_debug_msg("Starting new memcached_1_5_13 listen handler thread\n");
  memcached_1_5_13_handler_post_listen_arg_t *arg = (memcached_1_5_13_handler_post_listen_arg_t*)_arg;
  memcached_1_5_13_handler_t *self_hdl = arg->self_hdl;
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr = __net.net_addr;
  server_addr.sin_port = htons(MEMCACHED_PORT);
  char server_addr_str[MAX_SOCK_ADDRSTRLEN];
  _get_sockaddr_str(server_addr_str, sizeof(server_addr_str), AF_INET,
                    (const struct sockaddr*)&server_addr);
  posix_debug_msg("Attempting to connect to the server socket %s\n",
                  server_addr_str);
  int ret;
  ret = _stream_connect(self_hdl->client_sock,
                            (const struct sockaddr *)&server_addr,
                            sizeof(server_addr));
  assert(ret == 0 && "memcached_1_5_13 listen handler fails to connect to "
                      "the server socket");
  // char payload[1024];
  // const char val[] = "the_value";
  // snprintf(payload, sizeof(payload), "set the_key 1 0 %lu\n", sizeof(val));
  char payload[] = "set the_key 1 0 1\r\na\r\nshutdown\r\n";
  if (useSymbolicHandler) {
    klee_make_symbolic(payload, sizeof(payload), "memcached_1_5_13_payload");
    strncpy(payload, "set", 3);
    strncpy(payload + 22, "shutdown", 8);
  }
  ret = _write_socket(self_hdl->client_sock, payload, sizeof(payload) - 1);
  posix_debug_msg("payload (set) write result %d\n", ret);

  // sleep(30);

  // snprintf(payload, sizeof(payload), "%s\n", val);
  // ret = _write_socket(self_hdl->client_sock, payload, sizeof(payload));
  // posix_debug_msg("payload (val) write result %d\n", ret);

  // snprintf(payload, sizeof(payload), "shutdown\r\n");
  // const char shutdown[] = "shutdown\r\n";
  // ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  // posix_debug_msg("payload (shutdown) write result %d\n", ret);
  return NULL;
}

static void memcached_1_5_13_handler_post_listen(
    void *self, socket_t *sock, __attribute__((unused)) int backlog) {
  memcached_1_5_13_handler_t *self_hdl = (memcached_1_5_13_handler_t *)self;
  posix_debug_msg(
          "memcached_1_5_13 post listen handler self: %p\n", self_hdl);
  if (self_hdl->server_sock && self_hdl->server_sock == sock) {
    posix_debug_msg(
          "memcached_1_5_13 post listen handler create new thread\n");
    memcached_1_5_13_listen_arg.self_hdl = self_hdl;
    pthread_t th;
    int ret = pthread_create(&th, NULL,
                             memcached_1_5_13_handler_post_listen_newthread,
                             &memcached_1_5_13_listen_arg);
    if (ret != 0) {
      posix_debug_msg(
          "memcached_1_5_13 listen handler pthread failed with ret %d\n", ret);
    }
  }
}

static memcached_1_5_13_handler_t memcached_1_5_13_handler = {
    .__base = {
        .name = "memcached_1.5.13",
        .init = memcached_1_5_13_handler_init,
        .post_bind = memcached_1_5_13_handler_post_bind,
        .post_listen = memcached_1_5_13_handler_post_listen,
    },
    .client_sock = NULL,
    .server_sock = NULL,
};

/**
 * memcached update handler
 * 
 * This handler will issue symbolic set commands. After a static number of 
 * updates are sent, the update handler will issue a shutdown command.
 * 
 * If --symbolic-sock-handler is set, then the handler will decide the number 
 * of updates symbolically.
 */

static void memcached_update_handler_init(void *self) {
  memcached_update_handler_t *self_hdl = (memcached_update_handler_t*)self;
  self_hdl->client_sock = _create_socket(AF_INET, SOCK_STREAM, 0);
};

static void
memcached_update_handler_post_bind(void *self, socket_t *sock,
                                   const struct sockaddr *addr,
                                   socklen_t addrlen) {
  memcached_update_handler_t *self_hdl = (memcached_update_handler_t*)self;
  // I am only interested in socket bind to DEFAULT_ADDR:MEMCACHED_PORT
  if (sock->domain != AF_INET) {
    posix_debug_msg("memcached update bind handler: no bind, not AF_INET\n");
    return;
  }
  const struct sockaddr_in *inetaddr = (struct sockaddr_in*)addr;
  if (inetaddr->sin_addr.s_addr != __net.net_addr.s_addr) {
    posix_debug_msg("memcached update bind handler: no bind, bad saddr \n");
    return;
  }
  if (inetaddr->sin_port != htons(MEMCACHED_PORT)) {
    posix_debug_msg("memcached update bind handler: no bind, bad port \n");
    return;
  }
  self_hdl->server_sock = sock;
  posix_debug_msg("memcached update bind handler catch the server socket\n");
}

typedef struct {
  memcached_update_handler_t *self_hdl;
} memcached_update_handler_post_listen_arg_t;

memcached_update_handler_post_listen_arg_t memcached_update_listen_arg;

static void *memcached_update_handler_post_listen_newthread(void *_arg) {
  posix_debug_msg("Starting new memcached update listen handler thread\n");
  memcached_update_handler_post_listen_arg_t *arg = (memcached_update_handler_post_listen_arg_t*)_arg;
  memcached_update_handler_t *self_hdl = arg->self_hdl;

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr = __net.net_addr;
  server_addr.sin_port = htons(MEMCACHED_PORT);
  char server_addr_str[MAX_SOCK_ADDRSTRLEN];
  _get_sockaddr_str(server_addr_str, sizeof(server_addr_str), AF_INET,
                    (const struct sockaddr*)&server_addr);
  posix_debug_msg("[memcached_update] Attempting to connect to the server socket %s\n",
                  server_addr_str);
  int ret;
  ret = _stream_connect(self_hdl->client_sock,
                            (const struct sockaddr *)&server_addr,
                            sizeof(server_addr));
  assert(ret == 0 && "memcached update listen handler fails to connect to "
                      "the server socket");
  
  static const char flushSeq[] = "\r\n";
  int nwrites = 10;
  char payload[128];
  char keyData = 'A';

  if (useSymbolicHandler) {
    klee_make_symbolic(payload, sizeof(payload), "memcached_update_payload");

    klee_make_symbolic(&nwrites, sizeof(nwrites), "memcached_update_nwrites");
    klee_assume(nwrites <= 10);

    klee_make_symbolic(&keyData, sizeof(keyData), "memcached_update_key_data");
    klee_assume(keyData > ' ');
    klee_assume(keyData + 10 < '~');
  } 

  memset(payload, 0, sizeof(payload));

  int i;
  for (i = 0; i < nwrites; i++) {
    snprintf(payload, sizeof(payload), "set key_%c 1 0 5%sval_%c%s",
            keyData, flushSeq, keyData, flushSeq);
    ret = _write_socket(self_hdl->client_sock, payload, strlen(payload));
    posix_debug_msg("Write #%d: payload (set) write result %d\n", i, ret);
    keyData++;
  }
  
  const char shutdown[] = "shutdown\r\n";
  ret = _write_socket(self_hdl->client_sock, shutdown, sizeof(shutdown));
  posix_debug_msg("payload (shutdown) write result %d\n", ret);

  return NULL;
}

static void 
memcached_update_handler_post_listen(void *self, socket_t *sock, 
                                     __attribute__((unused)) int backlog) {
  memcached_update_handler_t *self_hdl = (memcached_update_handler_t *)self;
  posix_debug_msg(
          "memcached update post listen handler self: %p\n", self_hdl);
  if (self_hdl->server_sock && self_hdl->server_sock == sock) {
    posix_debug_msg(
          "memcached update post listen handler create new thread\n");
    memcached_update_listen_arg.self_hdl = self_hdl;
    pthread_t th;
    int ret = pthread_create(&th, NULL,
                             memcached_update_handler_post_listen_newthread,
                             &memcached_update_listen_arg);
    if (ret != 0) {
      posix_debug_msg(
          "memcached update listen handler pthread failed with ret %d\n", ret);
    }
  }
}

static memcached_update_handler_t memcached_update_handler = {
    .__base = {
        .name = "memcached_update",
        .init = memcached_update_handler_init,
        .post_bind = memcached_update_handler_post_bind,
        .post_listen = memcached_update_handler_post_listen,
    },
    .client_sock = NULL,
    .server_sock = NULL,
};
