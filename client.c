#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "utils.h"

static char *user_line = NULL;
static bool user_ended = false;

static void rl_handler(char *line) {
  if (!line) {
    user_ended = true;
    return;
  }
  user_line = line;
}

static int client_run(int socket_fd) {
  struct pollfd fds[2] = {
      {.fd = STDIN_FILENO, .events = POLLIN},
      {.fd = socket_fd, .events = POLLIN},
  };

  bool editing = false;

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
        if (editing)
          rl_callback_handler_remove();
        return 0;
      }

      uint32_t magic = ntohl(*(uint32_t *)header);
      if (magic != PROTO_MAGIC) {
        printf("\r\nprotocol error: invalid magic number\n");
        if (editing)
          rl_callback_handler_remove();
        return 1;
      }

      uint32_t len = ntohl(*(uint32_t *)(header + 4));
      char type = header[8];

      char buf[1024];
      if (len >= sizeof(buf)) {
        printf("\r\nmessage too large\n");
        if (editing)
          rl_callback_handler_remove();
        return 1;
      }
      if (recv_all(socket_fd, buf, len) <= 0) {
        printf("\r\nconnection closed\n");
        if (editing)
          rl_callback_handler_remove();
        return 0;
      }
      buf[len] = '\0';

      if (type == 'm') {
        if (editing) {
          write(STDOUT_FILENO, "\r\x1b[2K", 5);
        }
        fputs(buf, stdout);
        if (editing) {
          rl_forced_update_display();
        }
        fflush(stdout);
      } else if (type == 'p') {
        if (editing) {
          rl_set_prompt(buf);
          rl_forced_update_display();
        } else {
          rl_callback_handler_install(buf, rl_handler);
          editing = true;
        }
      }
    }

    // stdin -> send
    if (fds[0].revents & POLLIN) {
      if (!editing)
        continue;

      rl_callback_read_char();

      if (user_ended) {
        rl_callback_handler_remove();
        shutdown(socket_fd, SHUT_WR);
        return 0;
      }

      if (user_line) {
        editing = false;
        rl_callback_handler_remove();

        uint32_t magic = htonl(PROTO_MAGIC);
        uint32_t net_len = htonl((uint32_t)strlen(user_line));
        send_all(socket_fd, (char *)&magic, 4);
        send_all(socket_fd, (char *)&net_len, 4);
        send_all(socket_fd, user_line, strlen(user_line));

        free(user_line);
        user_line = NULL;
      }
    }

    // check for hangup
    if (fds[1].revents & (POLLHUP | POLLERR)) {
      if (editing)
        rl_callback_handler_remove();
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

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    log_perror(NULL, "socket");
    return errno;
  }
  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    log_perror(NULL, "connect");
    close(socket_fd);
    return errno;
  }

  int result = client_run(socket_fd);
  close(socket_fd);
  return result;
}
