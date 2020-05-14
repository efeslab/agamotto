#ifndef SOCKSIM_MEMCACHED_H_
#define SOCKSIM_MEMCACHED_H_

#include "client.h"

/**
 * Memcached socket simulators are basic simulated clients.
 */
typedef simulated_client_handler_t memcached_handler_t;

extern memcached_handler_t __memcached_1_5_13_handler;
extern memcached_handler_t __memcached_update_handler;

#endif
