#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>

typedef struct {
  const char *host;
  uint16_t port;
} client_options_t;

int client_start(client_options_t);

#endif // CLIENT_H
