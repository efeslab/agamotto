#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"

void simulated_client_handler_init(void *self) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;
  self_hdl->client_sock = _create_socket(AF_INET, SOCK_STREAM, 0);
  self_hdl->server_sock = NULL;
}

void simulated_client_handler_post_bind(void *self, socket_t *sock,
                                        const struct sockaddr *addr,
                                        socklen_t addrlen) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t*)self;
  int server_port = self_hdl->server_port;

  // We are only interested in socket bind to DEFAULT_ADDR:server_port
  if (sock->domain != AF_INET) {
    posix_debug_msg("simulated_client bind handler: no bind, not AF_INET\n");
    return;
  }
  const struct sockaddr_in *inetaddr = (struct sockaddr_in*)addr;
  if (inetaddr->sin_addr.s_addr != INADDR_ANY
      && inetaddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)
      && inetaddr->sin_addr.s_addr != __net.net_addr.s_addr) {
    posix_debug_msg("simulated_client bind handler: no bind, bad saddr \n");
    return;
  }
  if (inetaddr->sin_port != htons(server_port)) {
    posix_debug_msg("simulated_client bind handler: no bind, bad port \n");
    return;
  }

  self_hdl->server_sock = sock;
  posix_debug_msg("simulated_client bind handler catch the server socket\n");
}

static void *connect_and_run_client(void *self) {
  posix_debug_msg("Starting new simulated_client thread\n");
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr = __net.net_addr;
  server_addr.sin_port = htons(self_hdl->server_port);
  char server_addr_str[MAX_SOCK_ADDRSTRLEN];
  _get_sockaddr_str(server_addr_str, sizeof(server_addr_str), AF_INET,
                    (const struct sockaddr*)&server_addr);
  posix_debug_msg("Attempting to connect to the server socket %s\n",
                  server_addr_str);
  int ret;
  ret = _stream_connect(self_hdl->client_sock,
                        (const struct sockaddr *)&server_addr,
                        sizeof(server_addr));
  assert(ret == 0 && "simulated_client listen handler fails to connect to "
                      "the server socket");

  // Call the client_func to actually do the client's work
  self_hdl->client_func(self);

  _close_socket(self_hdl->client_sock);
  return NULL;
}

void simulated_client_handler_post_listen(void *self, socket_t *sock,
                                          int backlog) {
  simulated_client_handler_t *self_hdl = (simulated_client_handler_t *)self;
  posix_debug_msg("simulated_client post listen handler self: %p\n", self_hdl);

  if (self_hdl->server_sock && self_hdl->server_sock == sock) {
    posix_debug_msg("simulated_client post listen handler create new thread\n");
    pthread_t th;
    int ret = pthread_create(&th, NULL, connect_and_run_client, (void*)self_hdl);
    if (ret != 0) {
      posix_debug_msg(
          "simulated_client listen handler pthread failed with ret %d\n", ret);
    }
  }

  (void)backlog;
}

/**
 * Simple text client
 */

typedef struct {
  simulated_client_handler_t __base;
  const char *text;
  unsigned len;
} simple_text_client_handler_t;

static void simple_text_client_func(void *self) {
  socket_event_handler_t *base = (socket_event_handler_t *)self;
  simulated_client_handler_t *client = (simulated_client_handler_t *)base;
  simple_text_client_handler_t *text_client = (simple_text_client_handler_t *)base;

  const char *payload = text_client->text;
  const int len = text_client->len;

  _write_socket(client->client_sock, payload, len);
}

socket_event_handler_t *
create_simple_text_client(const char *name, int server_port,
                          const char *text, unsigned len) {
  simple_text_client_handler_t *handler;
  handler = malloc(sizeof(*handler));

  *handler = (simple_text_client_handler_t) {
    .__base = SIMULATED_CLIENT_HANDLER(name,
                                       server_port,
                                       simple_text_client_func),
    .text = text,
    .len = len,
  };

  return (socket_event_handler_t *)handler;
}

