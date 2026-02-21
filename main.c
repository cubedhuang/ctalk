#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "server.h"
#include "utils.h"

#define print_basic_usage()                                                    \
  fprintf(stderr,                                                              \
          ANSI_BOLD ANSI_BRIGHT_CYAN "usage: " ANSI_RESET ANSI_GREEN           \
                                     "%s <serve|join>\n",                      \
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
        print_err("invalid port '%s'", optarg);
        return 1;
      }
      break;
    case '?':
      print_err("unknown option '-%c'", optopt);
      return 1;
    case ':':
      print_err("missing argument after '-%c'", optopt);
      return 1;
    }
  }

  if (!host) {
    print_err("missing required option '-h' or '--host'");
    return 1;
  }
  if (port == -1) {
    print_err("missing required option '-p' or '--port'");
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
