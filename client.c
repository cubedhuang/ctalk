#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "utils.h"

static int socket_fd;

static int client_run() {
  struct pollfd fds[2] = {
      {.fd = STDIN_FILENO, .events = POLLIN},
      {.fd = socket_fd, .events = POLLIN},
  };

  char buf[1024];

  while (true) {
    if (poll(fds, 2, -1) == -1) {
      if (errno == EINTR)
        continue;
      perror("poll");
      return errno;
    }

    // stdin -> send
    if (fds[0].revents & POLLIN) {
      if (!fgets(buf, sizeof(buf), stdin)) {
        shutdown(socket_fd, SHUT_WR);
        break;
      }
      buf[strcspn(buf, "\n")] = '\0';
      if (send_all(socket_fd, buf) == -1) {
        perror("send");
        return errno;
      }
    }

    // recv -> print
    if (fds[1].revents & POLLIN) {
      ssize_t bytes = recv(socket_fd, buf, sizeof(buf) - 1, 0);
      if (bytes <= 0) {
        if (bytes == -1)
          perror("recv");
        printf("connection closed\n");
        return 0;
      }
      buf[bytes] = '\0';
      printf("%s", buf);
      fflush(stdout);
    }

    // check for hangup
    if (fds[1].revents & (POLLHUP | POLLERR)) {
      printf("connection closed\n");
      return 0;
    }
  }

  return 0;
}

int client_start(client_options_t options) {
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(options.port),
  };
  if (!inet_pton(AF_INET, options.host, &addr.sin_addr)) {
    print_err("malformed host ip address");
    return 1;
  }

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    perror("socket");
    return errno;
  }
  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("connect");
    close(socket_fd);
    return errno;
  }

  int result = client_run();
  close(socket_fd);
  return result;
}
