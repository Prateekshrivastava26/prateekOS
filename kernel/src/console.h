#ifndef MYOS_CONSOLE_H
#define MYOS_CONSOLE_H

#include <limine.h>

void console_init(struct limine_framebuffer *framebuffer);
void console_clear(void);
void console_putc(char character);
void console_write(const char *text);

#endif
