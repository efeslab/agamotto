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

static int randint(int min, int max) {
  assert(min <= max);
  return min + (rand() % (max - min + 1));
}

static const char linebreak[2] = {'\r', '\n'};

// Send a request, followed by \r\n.
// Return 0 on success, -1 on send failure.
static int _send_redis_req(socket_t *client_sock, const char *msg) {
  if (!klee_is_symbolic(msg[0]))
    posix_debug_msg("Sending redis request: '%s'\n", msg);

  int ret;

  int len = strlen(msg);
  int sent = 0;
  while (sent < len) {
    ret = _write_socket(client_sock, msg + sent, len - sent);
    if (ret < 0) return -1;
    sent += ret;
  }

  sent = 0;
  while (sent < 2) {
    ret = _write_socket(client_sock, linebreak + sent, 2 - sent);
    if (ret < 0) return -1;
    sent += ret;
  }

  return 0;
}

// Receives nresp responses from the redis server.
// Returns 0 on success, -2 on recv failure, and -3 on redis error
static int recv_redis_responses(socket_t *client_sock, int nresp) {
  int ret = 0;
  char buf[64];
  char first = 0; // first byte of current response
  int num_linebreaks = 0; // number of linebreaks expected for current response
  int redis_err = 0; // if any response begins with '-'
  char *srch, *newline;
  int responses = 0;
  while (responses < nresp) {
    ret = _read_socket(client_sock, buf, sizeof(buf) - 1);
    if (ret == 0) break;
    if (ret < 0) return -2;

    // Ensure null-terminated
    buf[ret] = 0;

    // Parse as many responses as we can from the buffer
    srch = buf;
    while (srch < buf + ret) {
      // responses starting with '$' have two newlines
      if (num_linebreaks == 0) {
        first = buf[0];
        if (first == '-')
          redis_err = 1;

        if (first == '$')
          num_linebreaks = 2;
        else
          num_linebreaks = 1;
      }

      newline = strchr(srch, '\r');
      if (newline == NULL)
        break;

      if (--num_linebreaks == 0) {
        // Got a response
        ++responses;
        *newline = 0;
        if (!klee_is_symbolic(*srch)) {
          if (first == '-') {
            printf("Received error ending in: '%s'\n", srch);
          } else {
            posix_debug_msg("Received response ending in: '%s'\n", srch);
          }
        }
      }

      srch = newline + 2;
    }
  }

  return redis_err ? -3 : 0;
}

// Send req followed by '\r\n' and wait for response.
// Return 0 on success, -1 on send failure, -2 on recv failure, -3 on redis error.
static int send_redis_req(socket_t *client_sock, const char *msg) {
  int ret = _send_redis_req(client_sock, msg);
  if (ret < 0)
    return ret;
  ret = recv_redis_responses(client_sock, 1);
  return ret;
}

// Send req followed by '\r\n'.
// Return 0 on success, -1 on send failure.
static int send_redis_req_noreply(socket_t *client_sock, const char *msg) {
  return _send_redis_req(client_sock, msg);
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
 * Simple concrete handler.
 */

#define DEFAULT_BATCH_SIZE 10

static void redis_file_client_func(void *);

redis_handler_t __redis_file_handler = {
  .__base = SIMULATED_CLIENT_HANDLER("redis_file",
                                     REDIS_PORT,
                                     redis_file_client_func),
};

#define EMITERR(msg) klee_report_error(__FILE__, __LINE__, msg, "user.err")

#define ATOI(arg, msg) ({ \
  int __i = atoi((arg)); \
  if (__i == 0) \
    EMITERR(msg); \
  __i; \
})

static void redis_file_argparse(void *self,
                                const char **file_out,
                                int *minbatch_out, int *maxbatch_out) {
  socket_event_handler_t *base = (socket_event_handler_t *)self;
  int argc = base->argc;
  const char **argv = base->argv;
  const char *msg = "redis_file socket handler expects a string argument "
    "<request-file> and either a single integer argument <batch-size> "
    "or two integer arguments <min-batch> <max-batch>";

  if (argc < 1) {
    EMITERR(msg);
  }

  *file_out = argv[0];

  if (argc == 2) {
    int batchsz = ATOI(argv[1], msg);
    *minbatch_out = batchsz;
    *maxbatch_out = batchsz;
  } else if (argc >= 3) {
    int minbatch = ATOI(argv[1], msg);
    int maxbatch = ATOI(argv[2], msg);
    *minbatch_out = minbatch;
    *maxbatch_out = maxbatch;
  }
}

static void redis_file_client_func(void *self) {
  /* socket_event_handler_t *base = (socket_event_handler_t *)self; */
  simulated_client_handler_t *client = (simulated_client_handler_t *)self;
  /* redis_handler_t *redis = (redis_handler_t *)self; */

  // (iangneal): XXX cheap symbex
  // unsigned seed = 0;
  // klee_make_symbolic(&seed, sizeof(seed), "rand seed");
  // klee_assume(seed <= 10);
  // srand(seed);
  size_t nops = 100;
  klee_make_symbolic(&nops, sizeof(nops), "nops");
  klee_assume(nops <= 100);
  size_t nprints = 0;
  //klee_make_symbolic(&nprints, sizeof(nprints), "nprints");

  const char *fname = NULL;
  int minbatch = DEFAULT_BATCH_SIZE;
  int maxbatch = DEFAULT_BATCH_SIZE;
  redis_file_argparse(self, &fname, &minbatch, &maxbatch);

  int ret = 0, i = 0, batchsz = minbatch;
  FILE *file = fopen(fname, "r");
  char req[2048];
  size_t total_ops = 0;
  while (fgets(req, sizeof(req), file)) {
    if (total_ops >= nops) break;
    total_ops++;
    //for (int i = 0; i < nprints; ++i) {
    //  printf("i=%d\n", i);
    //}

    if (i == 0) {
      batchsz = randint(minbatch, maxbatch);
      printf("Sending a batch of %i requests\n", batchsz);
    }

    char *newline = strchr(req, '\n');
    assert(newline != NULL && newline[-1] == '\r');
    // Exclude \r\n, send_redis_req will append it
    newline[-1] = 0;

    ret = send_redis_req_noreply(client->client_sock, req);
    if (ret != 0)
      break;

    if (++i % batchsz == 0) {
      i = 0;
      ret = recv_redis_responses(client->client_sock, batchsz);
      if (ret != 0)
        break;
      simulated_client_reconnect(client);
    }
  }

  // Receive any responses that weren't yet received.
  if (i != 0 && ret == 0) {
    ret = recv_redis_responses(client->client_sock, i);
  }

  ret || send_redis_req_noreply(client->client_sock, "SHUTDOWN");

  if (ret == -1)
    perror("Failed to send redis request");
  else if (ret == -2)
    perror("Failed to recv redis response");
  else if (ret == -3)
    fprintf(stderr, "Redis server responded with error.\n");

  fclose(file);
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

