#ifndef MYOS_TIMER_H
#define MYOS_TIMER_H

#include <stdint.h>

extern volatile uint64_t timer_ticks;

void timer_init(uint32_t frequency_hz);
void timer_irq_handler(void);

#endif
