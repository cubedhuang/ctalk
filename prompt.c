#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "prompt.h"

static struct termios orig_termios;
static char input_buf[1024];
static int input_len = 0;
static char current_prompt[128] = "";

void prompt_init(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void prompt_cleanup(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void prompt_set(const char *prompt_text) {
  strncpy(current_prompt, prompt_text, sizeof(current_prompt) - 1);
  current_prompt[sizeof(current_prompt) - 1] = '\0';
}

void prompt_hide(void) { write(STDOUT_FILENO, "\r\x1b[2K", 5); }

void prompt_show(void) {
  write(STDOUT_FILENO, "\r", 1);
  write(STDOUT_FILENO, current_prompt, strlen(current_prompt));
  write(STDOUT_FILENO, input_buf, input_len);
}

typedef enum {
  STATE_NORMAL,
  STATE_ESC, // seen '\x1b'
  STATE_CSI  // seen '\x1b['
} escape_state_t;

static escape_state_t esc_state = STATE_NORMAL;

char *prompt_feed(char c) {
  if (esc_state == STATE_ESC) {
    if (c == '[') {
      esc_state = STATE_CSI;
    } else {
      esc_state = STATE_NORMAL;
    }
    return NULL;
  } else if (esc_state == STATE_CSI) {
    // ANSI/VT100 sequences range
    if (c >= 0x40 && c <= 0x7E) {
      esc_state = STATE_NORMAL;
    }
    return NULL;
  }

  if (c == '\x1b') {
    esc_state = STATE_ESC;
    return NULL;
  }

  if (c == '\n' || c == '\r') {
    char *line = malloc(input_len + 1);
    memcpy(line, input_buf, input_len);
    line[input_len] = '\0';

    input_len = 0;
    write(STDOUT_FILENO, "\n", 1);
    return line;
  }

  if (c == '\x7f' || c == '\b') {
    // backspace
    if (input_len > 0) {
      input_len--;
      write(STDOUT_FILENO, "\b \b", 3);
    }
  } else if (c == '\x04') {
    // ctrl-d
    if (input_len == 0)
      return (char *)-1;
  } else if (c >= 32 && c <= 126) {
    // printables
    if (input_len < (int)(sizeof(input_buf) - 1)) {
      input_buf[input_len++] = c;
      write(STDOUT_FILENO, &c, 1);
    }
  }

  return NULL;
}
