#include <stdint.h>
#include "arch/x86_64/idt.h"

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

static idt_entry_t idt[256];

volatile uint8_t breakpoint_seen = 0;

extern void isr_timer(void);
extern void isr_breakpoint(void);
extern void isr_syscall(void);

static void set_idt_entry(uint8_t vector, void (*handler)(void),
                           uint8_t attributes) {
    uint64_t address = (uint64_t)handler;
    uint16_t code_selector;

    /* Use the code segment selector that Limine set for this kernel. */
    __asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));

    idt[vector] = (idt_entry_t) {
        .offset_low       = (uint16_t)(address & 0xffff),
        .selector         = code_selector,
        .ist              = 0,
        .type_attributes  = attributes,
        .offset_middle    = (uint16_t)((address >> 16) & 0xffff),
        .offset_high      = (uint32_t)(address >> 32),
        .reserved         = 0
    };
}

void idt_init(void) {
    
set_idt_entry(32, isr_timer, 0x8E);
set_idt_entry(3, isr_breakpoint, 0x8E);
    set_idt_entry(0x80, isr_syscall, 0xEE);

    idtr_t idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base  = (uint64_t)idt
    };

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
