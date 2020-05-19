#include <assert.h>
#include <stdlib.h>

#include "memcached.h"
#include "redis.h"
#include "sockets_simulator.h"

char useSymbolicHandler;
socket_simulator_t __sock_simulator;

typedef struct {
  const socket_event_handler_t *handler;
  size_t size;
} predefined_handler_t;

#define PREDEFINED(_name) \
  { (const socket_event_handler_t *)(&(_name)), sizeof(_name) }

static predefined_handler_t predefined_handlers[] = {
    PREDEFINED(__memcached_1_5_13_handler),
    PREDEFINED(__memcached_update_handler),
    PREDEFINED(__redis_simple_concrete_handler),
    PREDEFINED(__redis_file_handler),
    PREDEFINED(__redis_symbolic_handler),
    PREDEFINED(__redis_semi_symbolic_handler),
};

void klee_init_sockets_simulator(const socksim_init_descriptor_t *ssid) {
  __sock_simulator.registered_cnt = 0;
  memset(__sock_simulator.handlers, 0, sizeof(__sock_simulator.handlers));

  int i;
  for (i = 0; i < ssid->n_handlers; ++i) {
    register_socket_handler(ssid->handlers[i]);
  }
}

void register_socket_handler(socket_event_handler_t *hdl) {
  assert(__sock_simulator.registered_cnt < MAX_SOCK_EVT_HANDLE &&
         "Too many registered socket event handlers!");
  posix_debug_msg("Registering socket event handler: %s\n", hdl->name);
  __sock_simulator.handlers[__sock_simulator.registered_cnt++] = hdl;
  hdl->init(hdl);
}

socket_event_handler_t *
get_predefined_socket_handler(const char *name) {
  int i;
  for (i = 0; i < sizeof(predefined_handlers) / sizeof(predefined_handlers[0]); ++i) {
    const socket_event_handler_t *hdl = predefined_handlers[i].handler;
    size_t size = predefined_handlers[i].size;
    if (strcmp(name, hdl->name) == 0) {
      socket_event_handler_t *new_hdl = malloc(size);
      memcpy(new_hdl, hdl, size);
      return new_hdl;
    }
  }
  return NULL;
}
