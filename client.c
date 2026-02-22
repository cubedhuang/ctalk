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
#include "prompt.h"
#include "utils.h"

static int socket_fd;

static int client_run() {
  bool editing = false;
  prompt_init();

  struct pollfd fds[2] = {
      {.fd = STDIN_FILENO, .events = POLLIN},
      {.fd = socket_fd, .events = POLLIN},
  };

  while (true) {
    if (poll(fds, 2, -1) == -1) {
      if (errno == EINTR)
        continue;
      log_perror(NULL, "poll");
      return errno;
    }

    // server -> print
    if (fds[1].revents & POLLIN) {
      char header[9];
      if (recv_all(socket_fd, header, 9) <= 0) {
        printf("\r\nconnection closed\n");
        prompt_cleanup();
        return 0;
      }

      uint32_t magic = ntohl(*(uint32_t *)header);
      if (magic != PROTO_MAGIC) {
        printf("\r\nprotocol error: invalid magic number\n");
        prompt_cleanup();
        return 1;
      }

      uint32_t len = ntohl(*(uint32_t *)(header + 4));
      char type = header[8];

      char buf[1024];
      if (len >= sizeof(buf)) {
        printf("\r\nmessage too large\n");
        prompt_cleanup();
        return 1;
      }
      if (recv_all(socket_fd, buf, len) <= 0) {
        printf("\r\nconnection closed\n");
        prompt_cleanup();
        return 0;
      }
      buf[len] = '\0';

      if (type == 'm') {
        if (editing)
          prompt_hide();
        write(STDOUT_FILENO, buf, strlen(buf));
        if (editing)
          prompt_show();
      } else if (type == 'p') {
        prompt_set(buf);
        prompt_show();
        editing = true;
      }
    }

    // stdin -> send
    if (fds[0].revents & POLLIN) {
      if (!editing)
        continue;

      char c;
      if (read(STDIN_FILENO, &c, 1) <= 0)
        continue;

      char *line = prompt_feed(c);
      if (!line)
        continue;

      editing = false;

      if (line == (char *)-1) {
        shutdown(socket_fd, SHUT_WR);
        prompt_cleanup();
        return 0;
      }

      uint32_t magic = htonl(PROTO_MAGIC);
      uint32_t net_len = htonl((uint32_t)strlen(line));
      send_all(socket_fd, (char *)&magic, 4);
      send_all(socket_fd, (char *)&net_len, 4);
      send_all(socket_fd, line, strlen(line));

      free(line);
    }

    // check for hangup
    if (fds[1].revents & (POLLHUP | POLLERR)) {
      prompt_cleanup();
      printf("\r\nconnection closed\n");
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
    log_err(NULL, "malformed host ip address\n");
    return 1;
  }

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    log_perror(NULL, "socket");
    return errno;
  }
  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    log_perror(NULL, "connect");
    close(socket_fd);
    return errno;
  }

  int result = client_run();
  close(socket_fd);
  return result;
}
