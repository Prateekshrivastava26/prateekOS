#ifndef MYOS_KEYBOARD_H
#define MYOS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

bool keyboard_poll(uint8_t *scancode);

#endif
