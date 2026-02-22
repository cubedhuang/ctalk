#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

// LOGGING

static void log_prefix(const log_ctx_t *ctx, const char *level_color,
                       const char *level) {
  if (ctx) {
    fprintf(stderr,
            ANSI_BOLD "%s%s " ANSI_RESET ANSI_BOLD ANSI_BCYAN
                      "%s:%u " ANSI_RESET,
            level_color, level, ctx->ip, ctx->port);
  } else {
    fprintf(stderr, ANSI_BOLD "%s%s " ANSI_RESET, level_color, level);
  }
}

void log_info(const log_ctx_t *ctx, const char *fmt, ...) {
  log_prefix(ctx, ANSI_BGREEN, "info");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

void log_err(const log_ctx_t *ctx, const char *fmt, ...) {
  log_prefix(ctx, ANSI_BRED, "error");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

void log_perror(const log_ctx_t *ctx, const char *s) {
  log_prefix(ctx, ANSI_BRED, "error");
  fprintf(stderr, "%s: %s\n", s, strerror(errno));
}

// LOW LEVEL IO

int send_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t sent = send(fd, buf, len, 0);
    if (sent == -1)
      return -1;
    buf += sent;
    len -= sent;
  }
  return 0;
}

ssize_t recv_all(int fd, char *buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    ssize_t n = recv(fd, buf + received, len - received, 0);
    if (n <= 0)
      return n;
    received += n;
  }
  return (ssize_t)received;
}

// PROTOCOL

int proto_send(int fd, char type, const char *content) {
  size_t len = strlen(content);
  uint32_t net_len = htonl((uint32_t)len);
  uint32_t magic = htonl(PROTO_MAGIC);

  size_t total_len = 4 + 4 + 1 + len;
  char *full_msg = malloc(total_len);
  if (!full_msg)
    return -1;

  memcpy(full_msg, &magic, 4);
  memcpy(full_msg + 4, &net_len, 4);
  full_msg[8] = type;
  memcpy(full_msg + 9, content, len);

  int result = send_all(fd, full_msg, total_len);
  free(full_msg);
  return result;
}

ssize_t proto_recv(int fd, char *buf, size_t buf_len) {
  char header[8];
  ssize_t n = recv_all(fd, header, 8);
  if (n <= 0)
    return n;

  uint32_t received_magic = ntohl(*(uint32_t *)header);
  if (received_magic != PROTO_MAGIC) {
    log_err(NULL, "invalid magic number\n");
    return -1;
  }

  uint32_t len = ntohl(*(uint32_t *)(header + 4));
  if (len >= buf_len || len > 4096) {
    log_err(NULL, "proto_recv: message too large (%u bytes)\n", len);
    return -1;
  }

  n = recv_all(fd, buf, len);
  if (n <= 0)
    return n;

  buf[len] = '\0';
  return (ssize_t)len;
}

// HIGHER LEVEL IO

int io_message(client_io_t *io, const char *fmt, ...) {
  char buf[1152];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return proto_send(io->fd, 'm', buf);
}

ssize_t io_prompt(client_io_t *io, const char *fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (proto_send(io->fd, 'p', buf) == -1)
    return -1;
  return proto_recv(io->fd, io->buf, sizeof(io->buf));
}
