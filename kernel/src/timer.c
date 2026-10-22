#include <stdint.h>
#include "timer.h"
#include "task.h"

volatile uint64_t timer_ticks = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static void remap_pic(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();

    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Enable only the timer interrupt for now. */
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        return;
    }

    remap_pic();

    uint32_t divisor = 1193182 / frequency_hz;

    /* PIT channel 0, square-wave mode, low byte then high byte. */
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_irq_handler(void) {
    timer_ticks++;
    outb(0x20, 0x20); /* Acknowledge the interrupt to the master PIC. */
    task_schedule_tick();
}
