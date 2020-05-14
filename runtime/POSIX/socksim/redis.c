#include "redis.h"

#include "klee/klee.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define REDIS_PORT 6379

/**
 * Helper functions
 */

static int isprint(const char c) {
  /* Assume ASCII or null terminator */
  return ((32 <= c) & (c <= 126)) | (c == 0);
}

static char *get_sym_str(int numChars, char *name) {
  char *s = malloc(numChars+1);
  klee_make_symbolic(s, numChars+1, name);
  
  s[numChars] = 0;
  return s;
}

static const char linebreak[2] = {'\r', '\n'};

// Send a request, followed by \r\n, then optionally wait for server's response.
// Return -1 on send failure, -2 on recv failure, and -3 on redis error.
static int _send_redis_req(socket_t *client_sock, const char *msg,
                          int expect_response) {
  if (!klee_is_symbolic(msg[0]))
    posix_debug_msg("Sending redis request: '%s'\n", msg);

  int ret;

  int len = strlen(msg);
  ret = _write_socket(client_sock, msg, len);
  if (ret < 0) return -1;
  ret = _write_socket(client_sock, linebreak, 2);
  if (ret < 0) return -1;

  if (!expect_response)
    return 0;

  // Await response
  char buf[64];
  char first;
  int redis_err = 0;
  int num_linebreaks = -1;
  do {
    ret = _read_socket(client_sock, buf, sizeof(buf) - 1);
    if (ret == 0) break;
    if (ret < 0) return -2;

    // See how many \r\n we expect to get
    // (2 for responses starting with '$', 1 for all others).
    if (num_linebreaks == -1) {
      first = buf[0];
      if (first == '-')
        redis_err = 1;

      if (first == '$')
        num_linebreaks = 2;
      else
        num_linebreaks = 1;
    }

    // Ensure null-terminated
    buf[ret] = 0;

    // Check for \r
    char *srch = buf;
    char *newline;
    while (srch < buf + ret && (newline = strchr(srch, '\r')) != NULL) {
      --num_linebreaks;
      // Skip over \n and start at next character
      srch = newline + 2;
    }
  } while (num_linebreaks > 0);

  *strrchr(buf, '\r') = 0;
  if (!klee_is_symbolic(buf[0]))
    posix_debug_msg("Received response ending in: '%s'\n", buf);

  return redis_err ? -3 : 0;
}

static int send_redis_req(socket_t *client_sock, const char *msg) {
  return _send_redis_req(client_sock, msg, 1);
}

static int send_redis_req_noreply(socket_t *client_sock, const char *msg) {
  return _send_redis_req(client_sock, msg, 0);
}


/**
 * Simple concrete handler.
 */

static void redis_simple_concrete_client_func(void *);

redis_handler_t __redis_simple_concrete_handler = {
  .__base = SIMULATED_CLIENT_HANDLER("redis_simple_concrete",
                                     REDIS_PORT,
                                     redis_simple_concrete_client_func),
};

static void redis_simple_concrete_client_func(void *self) {
  /* socket_event_handler_t *base = (socket_event_handler_t *)self; */
  simulated_client_handler_t *client = (simulated_client_handler_t *)self;
  /* redis_handler_t *redis = (redis_handler_t *)self; */

  socket_t *sock = client->client_sock;

  int ret = 0;
  ret || (ret = send_redis_req(sock, "SET test hello"));
  ret || (ret = send_redis_req(sock, "APPEND test world"));
  ret || (ret = send_redis_req(sock, "GET test"));
  ret || (ret = send_redis_req(sock, "DEL test"));
  ret || (ret = send_redis_req_noreply(sock, "SHUTDOWN"));

  if (ret != 0) {
    perror("Failed to send redis request");
    return;
  }
}

/**
 * Symbolic handler
 */

#define PAYLOAD_MAX 20
#define NREQ_MAX 3

static void redis_symbolic_client_func(void *);

redis_handler_t __redis_symbolic_handler = {
  .__base = SIMULATED_CLIENT_HANDLER("redis_symbolic",
                                     REDIS_PORT,
                                     redis_symbolic_client_func),
};

static void redis_symbolic_client_func(void *self) {
  /* socket_event_handler_t *base = (socket_event_handler_t *)self; */
  simulated_client_handler_t *client = (simulated_client_handler_t *)self;
  /* redis_handler_t *redis = (redis_handler_t *)self; */

  int ret = 0;
  socket_t *sock = client->client_sock;

  // Send a symbolic number of requests, and then a SHUTDOWN
  unsigned nreq;
  klee_make_symbolic(&nreq, sizeof(nreq), "redis_num_requests");
  klee_assume(nreq <= NREQ_MAX);

  char *requests[NREQ_MAX];
  char sym_name[32];
  unsigned i, j;
  for (i = 0; i < nreq && ret == 0; ++i) {
    sprintf(sym_name, "redis_symbolic_request_%u", i);
    requests[i] = get_sym_str(PAYLOAD_MAX, sym_name);

    for (j = 0; j < PAYLOAD_MAX; ++j) {
      klee_assume(isprint(requests[i][j]));
    }
    klee_assume(requests[i][0] != '*');

    ret = send_redis_req(sock, requests[i]);
  }

  ret || (ret = send_redis_req_noreply(sock, "SHUTDOWN"));

  if (ret == -1)
    perror("Failed to send redis request");
  else if (ret == -2)
    perror("Failed to recv redis response");
  else if (ret == -3)
    fprintf(stderr, "Redis server responded with error.\n");
}

/**
 * Semi-Symbolic handler
 */

#undef PAYLOAD_MAX
#undef NREQ_MAX
#define PAYLOAD_MAX 20
#define NREQ_MAX 10

typedef enum {
  SET,
  DEL,
  RENAME,
  MSET,
  APPEND,
  SETRANGE,
  INCR,

  NUM_REQ_TYPES
} req_type;

#define TYPE_MAX RENAME

static void redis_semi_symbolic_client_func(void *);

redis_handler_t __redis_semi_symbolic_handler = {
  .__base = SIMULATED_CLIENT_HANDLER("redis_semi_symbolic",
                                     REDIS_PORT,
                                     redis_semi_symbolic_client_func),
};

static void redis_semi_symbolic_client_func(void *self) {
  /* socket_event_handler_t *base = (socket_event_handler_t *)self; */
  simulated_client_handler_t *client = (simulated_client_handler_t *)self;
  /* redis_handler_t *redis = (redis_handler_t *)self; */

  int ret = 0;
  socket_t *sock = client->client_sock;

  // Send a symbolic number of requests, and then a SHUTDOWN
  unsigned nreq;
  klee_make_symbolic(&nreq, sizeof(nreq), "redis_num_requests");
  klee_assume(nreq <= NREQ_MAX);

  // Make which type to send each time symbolic
  char types[NREQ_MAX];
  unsigned i;
  klee_make_symbolic(types, sizeof(types), "redis_request_types");
  for (i = 0; i < NREQ_MAX; ++i) {
    klee_assume(types[i] <= TYPE_MAX);
  }

  char *requests[NREQ_MAX];
  char sym_name[32];
  for (i = 0; i < nreq && ret == 0; ++i) {
    sprintf(sym_name, "redis_request_%u", i);
    char *req = requests[i] = get_sym_str(PAYLOAD_MAX, sym_name);

    switch (types[i]) {
    case SET:
      // SET ? val\0
      memcpy(req, "SET ", 4);
      klee_assume(req[4] == 'a' || req[4] == 'b');
      memcpy(req + 5, " val", 4);
      req[9] = 0;
      break;
    case DEL:
      // DEL ?\0
      memcpy(req, "DEL ", 4);
      klee_assume(req[4] == 'a' || req[4] == 'b');
      req[5] = 0;
      break;
    case RENAME:
      // RENAME ? ?\0
      memcpy(req, "RENAME ", 7);
      klee_assume(req[7] == 'a' || req[7] == 'b');
      req[8] = ' ';
      klee_assume(req[9] == 'a' || req[9] == 'b');
      req[10] = 0;
      break;
    default:
      continue;
    }

    ret = send_redis_req(sock, requests[i]);
  }

  ret || (ret = send_redis_req_noreply(sock, "SHUTDOWN"));

  if (ret == -1)
    perror("Failed to send redis request");
  else if (ret == -2)
    perror("Failed to recv redis response");
  else if (ret == -3)
    fprintf(stderr, "Redis server responded with error.\n");
}

