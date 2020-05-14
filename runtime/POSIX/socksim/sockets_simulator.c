#include <assert.h>

#include "memcached.h"
#include "sockets_simulator.h"

char useSymbolicHandler;
socket_simulator_t __sock_simulator;

static socket_event_handler_t *predefined_handlers[] = {
    (socket_event_handler_t *)(&__memcached_1_5_13_handler),
    (socket_event_handler_t *)(&__memcached_update_handler),
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

socket_event_handler_t *get_predefined_socket_handler(const char *name) {
  int i;
  for (i = 0; i < sizeof(predefined_handlers) / sizeof(predefined_handlers[0]); ++i) {
    socket_event_handler_t *hdl = predefined_handlers[i];
    if (strcmp(name, hdl->name) == 0) {
      return hdl;
    }
  }
  return NULL;
}
