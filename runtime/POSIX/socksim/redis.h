#ifndef SOCKSIM_REDIS_H_
#define SOCKSIM_REDIS_H_

#include "client.h"

/**
 * Memcached socket simulators are basic simulated clients.
 */
typedef struct {
  simulated_client_handler_t __base;
} redis_handler_t;

extern redis_handler_t __redis_simple_concrete_handler;
extern redis_handler_t __redis_symbolic_handler;
extern redis_handler_t __redis_semi_symbolic_handler;

#endif
