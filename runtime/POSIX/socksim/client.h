/**
 * This custom socket handler simulates a TCP client.
 *
 * When a socket binds to localhost on the given `server_port` and then listens,
 * a client socket is created and connected on a separate thread to the server.
 *
 * Then, the spawned thread runs the provided `client_func`, passing it a
 * pointer to its own simulated_client_handler_t.
 *
 * The most likely thing it will do is _write_socket to its `client_sock`.
 */

#ifndef SOCKSIM_CLIENT_H
#define SOCKSIM_CLIENT_H

#include "../sockets.h"
#include "sockets_simulator.h"

void simulated_client_handler_init(void *self);
void simulated_client_handler_post_bind(void *self, socket_t *sock,
                                        const struct sockaddr *addr,
                                        socklen_t addrlen);
void simulated_client_handler_post_listen(void *self, socket_t *sock,
                                          int backlog);

typedef void (*simulated_client_func_t)(void *);

typedef struct {
  socket_event_handler_t __base;
  // Specified ahead of time.
  int server_port;
  simulated_client_func_t client_func;
  // Set at runtime
  socket_t *server_sock;
  socket_t *client_sock;
} simulated_client_handler_t;

#define SIMULATED_CLIENT_HANDLER(_name, _port, _func) {    \
  .__base = {                                              \
    .name = _name,                                         \
    .init = simulated_client_handler_init,                 \
    .post_bind = simulated_client_handler_post_bind,       \
    .post_listen = simulated_client_handler_post_listen,   \
  },                                                       \
  .server_port = _port,                                    \
  .client_func = _func                                     \
}

/**
 * Creates a new simulated client handler that connects to `server_port`,
 * sends the payload `text` (length `len`), then closes.
 */
socket_event_handler_t *
create_simple_text_client(const char *name, int server_port,
                          const char *text, unsigned len);

/**
 * Creates a new simulated client handler that connects to `server_port`,
 * sends the contents of the file at `path`, then closes.
 */
socket_event_handler_t *
create_client_from_file(const char *name, int server_port,
                        const char *path);

#endif
