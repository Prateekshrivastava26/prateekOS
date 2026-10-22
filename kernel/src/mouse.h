#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

struct mouse_event {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
};

void mouse_init(void);
int mouse_poll(struct mouse_event *event);
void mouse_discard_pending(void);

#endif
