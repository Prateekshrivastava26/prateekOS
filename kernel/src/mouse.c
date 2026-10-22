#include "mouse.h"

#include <stdbool.h>
#include <stdint.h>

static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;
static uint8_t mouse_ready;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(0x64) & 2) == 0) return 1;
    }
    return 0;
}

static int wait_output_data(uint8_t *value) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(0x64) & 1) {
            *value = inb(0x60);
            return 1;
        }
    }
    return 0;
}

static int wait_aux_response(uint8_t *value) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(0x64);
        if ((status & 1) == 0) continue;
        uint8_t byte = inb(0x60);
        /* Some emulated controllers omit AUX on the mouse ACK byte. */
        if ((status & 0x20) || byte == 0xFA) {
            *value = byte;
            return 1;
        }
    }
    return 0;
}

static int mouse_command(uint8_t command) {
    if (!wait_input_clear()) return 0;
    outb(0x64, 0xD4);
    if (!wait_input_clear()) return 0;
    outb(0x60, command);
    uint8_t response;
    if (!wait_aux_response(&response)) return 0;
    return response == 0xFA;
}

void mouse_init(void) {
    mouse_packet_index = 0;
    mouse_ready = 0;
    /* Drain stale controller bytes before starting the mouse protocol. */
    for (uint32_t i = 0; i < 32 && (inb(0x64) & 1); i++) (void)inb(0x60);
    /* Enable the auxiliary clock; use polling instead of mouse IRQs. */
    if (!wait_input_clear()) return;
    outb(0x64, 0x20);
    uint8_t config;
    if (!wait_output_data(&config)) return;
    config = (uint8_t)((config & (uint8_t)~0x22) | 0x01);
    if (!wait_input_clear()) return;
    outb(0x64, 0x60);
    if (!wait_input_clear()) return;
    outb(0x60, config);
    if (!wait_input_clear()) return;
    outb(0x64, 0xA8); /* Enable the auxiliary PS/2 device. */
    /* Some emulators don't report the ACK with the AUX status bit set.
       Keep polling the device instead of disabling the pointer on that ACK. */
    (void)mouse_command(0xF6); /* Set defaults. */
    (void)mouse_command(0xF4); /* Enable mouse data reporting. */
    mouse_ready = 1;
}

void mouse_discard_pending(void) {
    /* The terminal does not use mouse packets; drain only auxiliary bytes. */
    for (uint32_t i = 0; i < 32; i++) {
        uint8_t status = inb(0x64);
        if ((status & 1) == 0 || (status & 0x20) == 0) break;
        (void)inb(0x60);
    }
    mouse_packet_index = 0;
}

int mouse_poll(struct mouse_event *event) {

    if (event == 0 || !mouse_ready) return 0;

    /* Drain a packet promptly; leave keyboard bytes for keyboard_poll(). */
    for (uint32_t drained = 0; drained < 16; drained++) {
        uint8_t status = inb(0x64);
        if ((status & 1) == 0 || (status & 0x20) == 0) return 0;

        uint8_t data = inb(0x60);
        if (mouse_packet_index == 0) {
            /* Bit 3 is always set in a valid first PS/2 packet byte. */
            if ((data & 0x08) == 0) continue;
            mouse_packet[mouse_packet_index++] = data;
            continue;
        }

        mouse_packet[mouse_packet_index++] = data;
        if (mouse_packet_index < 3) continue;
        mouse_packet_index = 0;

        /* Ignore saturated movement packets rather than jumping unpredictably. */
        if ((mouse_packet[0] & 0xC0) != 0) continue;
        event->buttons = mouse_packet[0] & 0x07;
        event->dx = (int8_t)mouse_packet[1];
        event->dy = (int8_t)mouse_packet[2];
        return 1;
    }
    return 0;
}
