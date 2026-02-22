#ifndef PROMPT_H
#define PROMPT_H

#include <stdbool.h>

void prompt_init(void);
void prompt_cleanup(void);
void prompt_set(const char *prompt_text);
void prompt_hide(void);
void prompt_show(void);
// feed one character in. returns the line if enter pressed, -1 on eof, NULL
// otherwise
char *prompt_feed(char c);

#endif // PROMPT_H
