#ifndef SOCKETS_SIMULATOR_H_
#define SOCKETS_SIMULATOR_H_
#include "../sockets.h"
// This flag denotes if sockets simulator is used during a symbolic replay
// Configurable in klee_init_env (option "-symbolic-sock-handler")
extern char useSymbolicHandler;
// a base structure every customizd simulator/handler should derive from
// NOTE that you should always put this structure to the first field of your
// customized structure
typedef struct {
  const char *name;
  int argc;
  const char **argv;
  void (*init)(void *self);
  void (*post_bind)(void *self, socket_t *sock,
                           const struct sockaddr *addr, socklen_t addrlen);
  void (*post_listen)(void *self, socket_t *sock, int backlog);
} socket_event_handler_t;

// klee_init_env will pass this to klee_init_sockets_simulator 
typedef struct {
  unsigned n_handlers;
  socket_event_handler_t *handlers[MAX_SOCK_EVT_HANDLE];
} socksim_init_descriptor_t;

typedef struct {
  unsigned int registered_cnt;
  socket_event_handler_t  *handlers[MAX_SOCK_EVT_HANDLE];
} socket_simulator_t;
extern socket_simulator_t __sock_simulator;
void klee_init_sockets_simulator(const socksim_init_descriptor_t *ssid);
void register_socket_handler(socket_event_handler_t *hdl);

// To see an example of a predefined handler, see memcached.h, memcached.c,
// and predefined_handlers inside sockets_simulator.c.
// @return NULL if no predefined handler for given name
socket_event_handler_t *
get_predefined_socket_handler(const char *name);

#define TRIGGER_SOCKET_HANDLER(handle, ...)                                    \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < __sock_simulator.registered_cnt; ++i) {                    \
      socket_event_handler_t *hdl = __sock_simulator.handlers[i];              \
      if (hdl->handle)                                                         \
        hdl->handle(hdl, __VA_ARGS__);                                         \
    }                                                                          \
  } while (0)
#endif // SOCKETS_SIMULATOR_H_
