#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int clients_add(client_context_t *ctx) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i]) {
    i++;
    if (i >= MAX_CLIENTS) {
      log_err(LOG_CTX(ctx), "too many clients!\n");
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

static int broadcast(int exclude, const char *fmt, ...) {
  char buf[1152];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

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
    if (proto_send(ctxs[i].client_fd, 'm', buf) == -1) {
      log_err(&(log_ctx_t){.ip = ctxs[i].ip, .port = ctxs[i].port},
              "broadcast failed\n");
    }
  }
  return 0;
}

static bool client_handshake(client_io_t *io, char *name_out, size_t name_len) {
  for (int attempts = 0; attempts < 3; attempts++) {
    ssize_t n = io_prompt(io, ANSI_BOLD ANSI_BYELLOW
                          "enter a display name: " ANSI_RESET);
    if (n <= 0)
      return false;

    if (strlen(io->buf) == 0) {
      if (io_message(io, "name cannot be empty, try again\n") == -1)
        return false;
      continue;
    }
    if (strlen(io->buf) >= name_len) {
      if (io_message(io, "name too long (max %d chars), try again\n",
                     name_len - 1) == -1)
        return false;
      continue;
    }

    strncpy(name_out, io->buf, name_len - 1);
    name_out[name_len - 1] = '\0';
    return true;
  }

  io_message(io, "too many failed attempts, disconnecting\n");
  return false;
}

static void *handle_client(void *ctx_raw) {
  client_context_t *ctx = ctx_raw;
  client_io_t io = {.fd = ctx->client_fd};
  log_info(LOG_CTX(ctx), "started on thread %p\n", (void *)pthread_self());

  char display_name[33];
  if (!client_handshake(&io, display_name, sizeof(display_name))) {
    log_info(LOG_CTX(ctx), "disconnected during handshake\n");
    goto cleanup;
  }

  if (clients_add(ctx) != 0) {
    io_message(&io, "server is full, please try again later\n");
    log_info(LOG_CTX(ctx), "rejected (server full)\n");
    goto cleanup;
  }

  log_info(LOG_CTX(ctx), "joined as '%s'\n", display_name);
  broadcast(-1,
            ANSI_BOLD ANSI_BCYAN "%s:%u " ANSI_RESET
                                 "joined as " ANSI_BOLD ANSI_BMAGENTA
                                 "%s" ANSI_RESET "\n",
            ctx->ip, ctx->port, display_name);

  ssize_t bytes;
  while ((bytes = io_prompt(&io, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET,
                            display_name)) > 0) {
    log_info(LOG_CTX(ctx), "message: %s\n", io.buf);
    broadcast(ctx->client_fd, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET "%s\n",
              display_name, io.buf);
  }
  if (bytes == -1) {
    log_perror(LOG_CTX(ctx), "io_prompt");
  }

  clients_remove(ctx->client_fd);
  broadcast(-1, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET "disconnected\n",
            display_name);
  log_info(LOG_CTX(ctx), "disconnected\n");

cleanup:
  close(ctx->client_fd);
  free(ctx);
  return NULL;
}

int server_start() {
  signal(SIGPIPE, SIG_IGN);

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    log_perror(NULL, "socket");
    return errno;
  }

  // allow reuse of port
  int opt = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    log_perror(NULL, "setsockopt");
    goto error;
  }

  // bind to port and start listening
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(8080),
      .sin_addr.s_addr = INADDR_ANY,
  };
  if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    log_perror(NULL, "bind");
    goto error;
  }
  if (listen(socket_fd, 10) == -1) {
    log_perror(NULL, "listen");
    goto error;
  }

  log_info(NULL, "started listening on port 8080\n");

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
      log_perror(NULL, "accept");
      goto error;
    }

    char client_ip[INET_ADDRSTRLEN];
    uint16_t client_port = ntohs(client_addr.sin_port);
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    log_info(NULL, "accepted connection from %s:%u\n", client_ip, client_port);

    // box information to pass into client handler
    client_context_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
      log_perror(NULL, "malloc");
      close(client_fd);
      goto error;
    }
    ctx->client_fd = client_fd;
    ctx->port = client_port;
    memcpy(ctx->ip, client_ip, sizeof(client_ip));

    pthread_t client_tid;
    int err = pthread_create(&client_tid, NULL, handle_client, ctx);
    if (err) {
      fprintf(stderr, "pthread_create: %s\n", strerror(err));
      free(ctx);
      close(client_fd);
      close(socket_fd);
      return err;
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
