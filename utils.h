#ifndef UTILS_H
#define UTILS_H

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

#define ANSI_BRIGHT_BLACK "\x1b[90m"
#define ANSI_BRIGHT_RED "\x1b[91m"
#define ANSI_BRIGHT_GREEN "\x1b[92m"
#define ANSI_BRIGHT_YELLOW "\x1b[93m"
#define ANSI_BRIGHT_BLUE "\x1b[94m"
#define ANSI_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_BRIGHT_CYAN "\x1b[96m"
#define ANSI_BRIGHT_WHITE "\x1b[97m"

#define print_err(description, ...)                                            \
  fprintf(stderr,                                                              \
          ANSI_BOLD ANSI_BRIGHT_RED "error: " ANSI_RESET ANSI_BOLD description \
                                    "\n" __VA_OPT__(, ) __VA_ARGS__)

int send_all(int fd, char *buf);

#endif // UTILS_H
