#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "server.h"
#include "utils.h"

#define print_basic_usage()                                                    \
  fprintf(stderr,                                                              \
          ANSI_BOLD ANSI_BCYAN "usage: " ANSI_RESET ANSI_GREEN                 \
                               "%s <serve|join>\n",                            \
          argv[0])

static struct option long_options[] = {
    {"host", required_argument, 0, 'h'},
    {"port", required_argument, 0, 'p'},
    {},
};

int handle_join(int argc, char *argv[static argc]) {
  char *host = nullptr;
  int port = -1;

  int opt;
  optind = 2;
  while ((opt = getopt_long(argc, argv, ":h:p:", long_options, NULL)) != -1) {
    switch (opt) {
    case 'h':
      host = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      if (port < 0 || port > UINT16_MAX) {
        log_err(NULL, "invalid port '%s'\n", optarg);
        return 1;
      }
      break;
    case '?':
      log_err(NULL, "unknown option '-%c'\n", optopt);
      return 1;
    case ':':
      log_err(NULL, "missing argument after '-%c'\n", optopt);
      return 1;
    }
  }

  if (!host) {
    log_err(NULL, "missing required option '-h' or '--host'\n");
    return 1;
  }
  if (port == -1) {
    log_err(NULL, "missing required option '-p' or '--port'\n");
    return 1;
  }

  return client_start((client_options_t){
      .host = host,
      .port = port,
  });
}

int main(int argc, char *argv[static argc]) {
  if (argc < 2) {
    print_basic_usage();
    return 1;
  }

  if (strcmp(argv[1], "serve") == 0) {
    return server_start();
  } else if (strcmp(argv[1], "join") == 0) {
    return handle_join(argc, argv);
  } else {
    print_basic_usage();
    return 1;
  }
}
