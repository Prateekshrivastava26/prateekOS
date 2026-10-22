#include <stdbool.h>
#include <stdint.h>
#include "keyboard.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool keyboard_poll(uint8_t *scancode) {
    if (scancode == 0) return false;
    /* Ignore auxiliary-device bytes so mouse packets are not keys. */
    uint8_t status = inb(0x64);
    if ((status & 1) == 0 || (status & 0x20) != 0) return false;

    uint8_t data = inb(0x60);
    if (status & 0xC0) return false; /* Discard parity/timeout errors. */
    *scancode = data;
    return true;
}
