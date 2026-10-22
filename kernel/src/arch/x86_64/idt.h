#ifndef MYOS_IDT_H
#define MYOS_IDT_H

#include <stdint.h>

extern volatile uint8_t breakpoint_seen;

void idt_init(void);

#endif
