#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#define MAX_CLIENTS 64
#define MAX_NAME_LEN 32

typedef struct {
  int fd;
  char ip[INET_ADDRSTRLEN];
  uint16_t port;
  char name[MAX_NAME_LEN + 1];
} client_ctx_t;

// guaranteed null sentinel
static client_ctx_t *clients[MAX_CLIENTS + 1];
static pthread_mutex_t clients_mu = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
  CLIENTS_ADD_OK,
  CLIENTS_ADD_ERROR,
  CLIENTS_ADD_DUPLICATE,
} clients_add_result_t;

static clients_add_result_t clients_add(client_ctx_t *ctx) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i]) {
    if (strcmp(clients[i]->name, ctx->name) == 0) {
      pthread_mutex_unlock(&clients_mu);
      return CLIENTS_ADD_DUPLICATE;
    }
    i++;
    if (i >= MAX_CLIENTS) {
      log_err(LOG_CTX(ctx), "too many clients!\n");
      pthread_mutex_unlock(&clients_mu);
      return CLIENTS_ADD_ERROR;
    }
  }

  clients[i] = ctx;
  pthread_mutex_unlock(&clients_mu);
  return CLIENTS_ADD_OK;
}

int clients_rename(int client_fd, const char *new_name) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i]) {
    if (strcmp(clients[i]->name, new_name) == 0) {
      pthread_mutex_unlock(&clients_mu);
      return 1;
    }
    if (clients[i]->fd == client_fd) {
      strncpy(clients[i]->name, new_name, MAX_NAME_LEN);
      clients[i]->name[MAX_NAME_LEN] = '\0';
    }
    i++;
  }

  pthread_mutex_unlock(&clients_mu);
  return 0;
}

static int clients_remove(int client_fd) {
  pthread_mutex_lock(&clients_mu);

  int i = 0;
  while (clients[i] && clients[i]->fd != client_fd) {
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

static size_t clients_clone(client_ctx_t *out, int exclude_fd) {
  pthread_mutex_lock(&clients_mu);

  int count = 0;
  for (int i = 0; clients[i]; i++) {
    if (clients[i]->fd != exclude_fd) {
      out[count++] = *clients[i];
    }
  }

  pthread_mutex_unlock(&clients_mu);
  return count;
}

static int broadcast(int exclude_fd, const char *fmt, ...) {
  char buf[1152];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  client_ctx_t ctxs[MAX_CLIENTS];
  size_t count = clients_clone(ctxs, exclude_fd);

  for (size_t i = 0; i < count; i++) {
    if (proto_send(ctxs[i].fd, 'm', buf) == -1) {
      log_err(&(log_ctx_t){.ip = ctxs[i].ip, .port = ctxs[i].port},
              "broadcast failed\n");
    }
  }
  return 0;
}

typedef enum {
  HANDSHAKE_OK,
  HANDSHAKE_DUPLICATE,
  HANDSHAKE_TOO_LONG,
  HANDSHAKE_EMPTY,
  HANDSHAKE_ERROR,
} handshake_result_t;

static handshake_result_t client_try_handshake(client_io_t *io,
                                               client_ctx_t *ctx) {
  ssize_t n =
      io_prompt(io, ANSI_BOLD ANSI_BYELLOW "enter a display name " ANSI_RESET);
  if (n <= 0)
    return HANDSHAKE_ERROR;
  if (strlen(io->buf) == 0)
    return HANDSHAKE_EMPTY;
  if (strlen(io->buf) > MAX_NAME_LEN)
    return HANDSHAKE_TOO_LONG;

  strncpy(ctx->name, io->buf, MAX_NAME_LEN);
  ctx->name[MAX_NAME_LEN] = '\0';

  return clients_add(ctx) == CLIENTS_ADD_DUPLICATE ? HANDSHAKE_DUPLICATE
                                                   : HANDSHAKE_OK;
}

typedef enum {
  CMD_OK,
  CMD_QUIT,
} cmd_result_t;

typedef cmd_result_t (*cmd_handler)(client_io_t *io, client_ctx_t *ctx);

typedef struct {
  const char *name;
  const char *help;
  cmd_handler handler;
} cmd_t;

static cmd_result_t cmd_help(client_io_t *io, client_ctx_t *ctx);
static cmd_result_t cmd_users(client_io_t *io, client_ctx_t *ctx);
static cmd_result_t cmd_quit(client_io_t *io, client_ctx_t *ctx);

static const cmd_t cmds[] = {
    {"help", "show this menu", cmd_help},
    {"users", "list connected users", cmd_users},
    {"quit", "disconnect", cmd_quit},
};

static cmd_result_t cmd_help(client_io_t *io, client_ctx_t *) {
  size_t count = sizeof(cmds) / sizeof(cmds[0]);
  for (size_t i = 0; i < count; i++) {
    io_message(
        io, ANSI_BOLD ANSI_BGREEN "    /%-8s" ANSI_RESET "  " ANSI_CYAN "%s\n",
        cmds[i].name, cmds[i].help);
  }
  return CMD_OK;
}
static cmd_result_t cmd_users(client_io_t *io, client_ctx_t *) {
  client_ctx_t clients[MAX_CLIENTS];
  size_t count = clients_clone(clients, -1);

  size_t longest_name = 0;
  for (size_t i = 0; i < count; i++) {
    size_t name_len = strlen(clients[i].name);
    if (name_len > longest_name) {
      longest_name = name_len;
    }
  }

  for (size_t i = 0; i < count; i++) {
    io_message(io,
               ANSI_BOLD ANSI_BBLACK "    %-*s" ANSI_RESET ANSI_CYAN
                                     "  %s:%u\n" ANSI_RESET,
               longest_name, clients[i].name, clients[i].ip, clients[i].port);
  }
  return CMD_OK;
}
static cmd_result_t cmd_quit(client_io_t *, client_ctx_t *) { return CMD_QUIT; }

static cmd_result_t handle_client_command(client_io_t *io, client_ctx_t *ctx) {
  char *cmd_str = strtok(io->buf + 1, " ");
  if (!cmd_str) {
    io_message(io, ANSI_BOLD ANSI_BRED "error " ANSI_RESET "invalid command\n");
    return CMD_OK;
  }

  for (int i = 0; cmds[i].name; i++) {
    if (strcmp(cmd_str, cmds[i].name) == 0) {
      return cmds[i].handler(io, ctx);
    }
  }

  io_message(io, ANSI_BOLD ANSI_BRED "error " ANSI_RESET "unknown command\n");
  return CMD_OK;
}

static void *handle_client(void *ctx_raw) {
  client_ctx_t *ctx = ctx_raw;
  client_io_t io = {.fd = ctx->fd};
  log_info(LOG_CTX(ctx), "started on thread %p\n", (void *)pthread_self());

  for (int attempts = 0; attempts < 3; attempts++) {
    switch (client_try_handshake(&io, ctx)) {
    case HANDSHAKE_OK:
      goto handshake_done;
    case HANDSHAKE_DUPLICATE:
      io_message(&io, ANSI_BOLD ANSI_BRED
                 "error " ANSI_RESET
                 "name already taken, please choose another\n");
      log_info(LOG_CTX(ctx), "handshake attempt %d: duplicate name '%s'\n",
               attempts + 1, io.buf);
      break;
    case HANDSHAKE_TOO_LONG:
      io_message(&io,
                 ANSI_BOLD ANSI_BRED
                 "error " ANSI_RESET
                 "name must be at most %d characters long\n",
                 MAX_NAME_LEN);
      log_info(LOG_CTX(ctx), "handshake attempt %d: name too long '%s'\n",
               attempts + 1, io.buf);
      break;
    case HANDSHAKE_EMPTY:
      io_message(&io, ANSI_BOLD ANSI_BRED "error " ANSI_RESET
                                          "name cannot be empty\n");
      log_info(LOG_CTX(ctx), "handshake attempt %d: empty name\n",
               attempts + 1);
      break;
    case HANDSHAKE_ERROR:
      log_info(LOG_CTX(ctx),
               "handshake attempt %d: error during prompt, disconnecting\n",
               attempts + 1);
      goto cleanup;
    }
  }

  io_message(&io, "too many failed attempts, disconnecting\n");
  log_info(LOG_CTX(ctx), "handshake failed: too many attempts\n");
  goto cleanup;

handshake_done:
  log_info(LOG_CTX(ctx), "joined as '%s'\n", ctx->name);
  broadcast(-1,
            ANSI_BOLD ANSI_BCYAN "%s:%u " ANSI_RESET
                                 "joined as " ANSI_BOLD ANSI_BMAGENTA
                                 "%s" ANSI_RESET "\n",
            ctx->ip, ctx->port, ctx->name);

  ssize_t bytes;
  while ((bytes = io_prompt(&io, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET,
                            ctx->name)) > 0) {
    log_info(LOG_CTX(ctx), "message: %s\n", io.buf);

    if (io.buf[0] == '/') {
      cmd_result_t result = handle_client_command(&io, ctx);
      if (result == CMD_QUIT) {
        break;
      }
    } else {
      broadcast(ctx->fd, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET "%s\n",
                ctx->name, io.buf);
    }
  }
  if (bytes == -1) {
    log_perror(LOG_CTX(ctx), "io_prompt");
  }

  clients_remove(ctx->fd);
  broadcast(-1, ANSI_BOLD ANSI_BMAGENTA "%s " ANSI_RESET "disconnected\n",
            ctx->name);
  log_info(LOG_CTX(ctx), "disconnected\n");

cleanup:
  close(ctx->fd);
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
    client_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
      log_perror(NULL, "malloc");
      close(client_fd);
      goto error;
    }
    ctx->fd = client_fd;
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
