#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

int send_all(int fd, char *buf) {
  size_t remaining = strlen(buf);
  while (remaining > 0) {
    ssize_t sent = send(fd, buf, remaining, 0);
    if (sent == -1) {
      return -1;
    }
    remaining -= sent;
    buf += sent;
  }

  return 0;
}
