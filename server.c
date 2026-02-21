#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_ssize_t.h>
#include <unistd.h>

#include "utils.h"

typedef struct {
  int client_fd;
  char ip[INET_ADDRSTRLEN];
  uint16_t port;
} client_context_t;

#define MAX_CLIENTS 64

// guaranteed null sentinel
static client_context_t *clients[MAX_CLIENTS + 1];
static pthread_mutex_t clients_mu = PTHREAD_MUTEX_INITIALIZER;

static void main_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf(ANSI_BOLD ANSI_BRIGHT_GREEN "log " ANSI_RESET);
  vprintf(fmt, args);
  va_end(args);
}

static void client_log(client_context_t *ctx, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf(ANSI_BOLD ANSI_BRIGHT_CYAN "%s:%u " ANSI_RESET, ctx->ip, ctx->port);
  vprintf(fmt, args);
  va_end(args);
}

static void client_error(client_context_t *ctx, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf(ANSI_BOLD ANSI_BRIGHT_RED "%s:%u error: " ANSI_RESET, ctx->ip,
         ctx->port);
  vprintf(fmt, args);
  va_end(args);
}

static void client_perror(client_context_t *ctx, const char *s) {
  fprintf(stderr, ANSI_BOLD ANSI_BRIGHT_RED "%s:%u %s: %s\n" ANSI_RESET,
          ctx->ip, ctx->port, s, strerror(errno));
}

static int clients_add(client_context_t *ctx) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i]) {
    i++;
    if (i >= MAX_CLIENTS) {
      client_error(ctx, "too many clients!\n");
      pthread_mutex_unlock(&clients_mu);
      return 1;
    }
  }

  clients[i] = ctx;
  pthread_mutex_unlock(&clients_mu);
  return 0;
}

static int clients_remove(int client_fd) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i] && clients[i]->client_fd != client_fd) {
    i++;
  }
  if (!clients[i]) {
    pthread_mutex_unlock(&clients_mu);
    return 1;
  }

  for (int j = i; clients[j]; j++) {
    clients[j] = clients[j + 1];
  }
  pthread_mutex_unlock(&clients_mu);
  return 0;
}

static int broadcast(char *message, int exclude) {
  // copy values to prevent network blocking
  pthread_mutex_lock(&clients_mu);
  client_context_t ctxs[MAX_CLIENTS + 1];
  int count = 0;
  for (int i = 0; clients[i]; i++) {
    if (clients[i]->client_fd != exclude)
      ctxs[count++] = *clients[i];
  }
  pthread_mutex_unlock(&clients_mu);

  for (int i = 0; i < count; i++) {
    if (send_all(ctxs[i].client_fd, message)) {
      client_error(&ctxs[i], "unable to receive message\n");
    }
  }
  return 0;
}

static void *handle_client(void *ctx_raw) {
  client_context_t *ctx = (client_context_t *)ctx_raw;
  clients_add(ctx);
  client_log(ctx, "started on thread %p\n", (void *)pthread_self());

  char buf[1024];
  ssize_t bytes;
  while ((bytes = recv(ctx->client_fd, buf, sizeof(buf) - 1, 0)) > 0) {
    buf[bytes] = '\0';

    client_log(ctx, "message: %s\n", buf);

    char fmt_buf[1152];
    snprintf(fmt_buf, sizeof(fmt_buf),
             ANSI_BOLD ANSI_BRIGHT_MAGENTA "%s:%u " ANSI_RESET "%s\n", ctx->ip,
             ctx->port, buf);
    broadcast(fmt_buf, ctx->client_fd);
  }
  if (bytes == -1) {
    client_perror(ctx, "recv");
  }

  clients_remove(ctx->client_fd);
  close(ctx->client_fd);
  client_log(ctx, "connection closed\n");
  free(ctx);
  return NULL;
}

int server_start() {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    perror("socket");
    return errno;
  }

  // allow reuse of port
  int opt = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    perror("setsockopt");
    goto error;
  }

  // bind to port and start listening
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(8080),
      .sin_addr.s_addr = INADDR_ANY,
  };
  if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    goto error;
  }
  if (listen(socket_fd, 10) == -1) {
    perror("listen");
    goto error;
  }

  main_log("started listening on port 8080\n");

  while (true) {
    // accept with ip
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(socket_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd == -1) {
      // try again on signal interrupt
      if (errno == EINTR)
        continue;
      perror("accept");
      goto error;
    }

    char client_ip[INET_ADDRSTRLEN];
    uint16_t client_port = ntohs(client_addr.sin_port);
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    main_log("accepted connection from %s:%u\n", client_ip, client_port);

    // box information to pass into client handler
    client_context_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
      perror("out of memory");
      close(client_fd);
      goto error;
    }
    ctx->client_fd = client_fd;
    ctx->port = client_port;
    memcpy(&ctx->ip, client_ip, sizeof(client_ip));

    pthread_t client_tid;
    int pthread_err = pthread_create(&client_tid, NULL, handle_client, ctx);
    if (pthread_err) {
      fprintf(stderr, "pthread_create: %s\n", strerror(pthread_err));
      free(ctx);
      close(client_fd);
      close(socket_fd);
      return pthread_err;
    }

    // detaching will free its resources on its own
    assert(pthread_detach(client_tid) == 0);
  }

  close(socket_fd);
  return 0;

error:
  int saved = errno;
  close(socket_fd);
  return saved;
}
