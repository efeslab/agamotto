#include "sockets_simulator.h"

char useSymbolicHandler;
socket_simulator_t __sock_simulator;

extern const socket_event_handler_t *__memcached_1_5_13_handler;
extern const socket_event_handler_t *__memcached_update_handler;

static socket_event_handler_t *all_handlers[] = {
    (socket_event_handler_t *)(&__memcached_1_5_13_handler),
    (socket_event_handler_t *)(&__memcached_update_handler),
};

void klee_init_sockets_simulator() {
  __sock_simulator.registered_cnt = 0;
  memset(__sock_simulator.handlers, 0, sizeof(__sock_simulator.handlers));
}

void register_predefined_socket_handler(const char *handler_name) {
  int i;
  for (i = 0; i < sizeof(all_handlers) / sizeof(all_handlers[0]); ++i) {
    socket_event_handler_t *hdl = all_handlers[i];
    if (strcmp(handler_name, hdl->name) == 0) {
      __sock_simulator.handlers[__sock_simulator.registered_cnt++] = hdl;
      hdl->init(hdl);
      return;
    }
  }
  posix_debug_msg("Ignore unknown socket handler %s\n", handler_name);
}

