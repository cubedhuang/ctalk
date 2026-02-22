#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <unistd.h>

#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD "\x1b[1m"

#define ANSI_BLACK "\x1b[30m"
#define ANSI_RED "\x1b[31m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_WHITE "\x1b[37m"

// bright versions of the above

#define ANSI_BBLACK "\x1b[90m"
#define ANSI_BRED "\x1b[91m"
#define ANSI_BGREEN "\x1b[92m"
#define ANSI_BYELLOW "\x1b[93m"
#define ANSI_BBLUE "\x1b[94m"
#define ANSI_BMAGENTA "\x1b[95m"
#define ANSI_BCYAN "\x1b[96m"
#define ANSI_BWHITE "\x1b[97m"

// LOGGING

typedef struct {
  const char *ip;
  uint16_t port;
} log_ctx_t;

void log_info(const log_ctx_t *ctx, const char *fmt, ...);
void log_err(const log_ctx_t *ctx, const char *fmt, ...);
void log_perror(const log_ctx_t *ctx, const char *s);

#define LOG_CTX(ctx) (&(log_ctx_t){.ip = (ctx)->ip, .port = (ctx)->port})

// LOW LEVEL IO
int send_all(int fd, const char *buf, size_t len);
ssize_t recv_all(int fd, char *buf, size_t len);

// PROTOCOL
#define PROTO_MAGIC 0x43484154
// server message: <length: 4 BE><type: 1><content>
int proto_send(int fd, char type, const char *content);
// client message: <length: 4 BE><content>
ssize_t proto_recv(int fd, char *buf, size_t buf_len);

// HIGHER LEVEL IO

typedef enum {
  SERVER_PROMPT = 'p',
  SERVER_MESSAGE = 'm',
} server_message_e;

typedef struct {
  int fd;
  char buf[1024];
} client_io_t;

// like recv, returns bytes read, 0 on close, -1 on error
int io_message(client_io_t *io, const char *fmt, ...);
ssize_t io_prompt(client_io_t *io, const char *fmt, ...);

#endif // UTILS_H
