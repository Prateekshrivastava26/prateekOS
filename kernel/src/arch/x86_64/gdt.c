#include "gdt.h"

#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

static uint64_t gdt[7];
static tss64_t tss;
static uint8_t kernel_interrupt_stack[16384] __attribute__((aligned(16)));

extern void gdt_flush(const gdtr_t *gdtr);

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL; /* Kernel code: selector 0x08 */
    gdt[2] = 0x00CF92000000FFFFULL; /* Kernel data: selector 0x10 */
    gdt[3] = 0x00CFF2000000FFFFULL; /* User data:   selector 0x1B */
    gdt[4] = 0x00AFFA000000FFFFULL; /* User code:   selector 0x23 */

    tss.rsp0 = (uint64_t)(uintptr_t)
        (&kernel_interrupt_stack[sizeof(kernel_interrupt_stack)]);
    tss.iomap_base = sizeof(tss);

    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;

    gdt[5] = (limit & 0xFFFF)
           | ((base & 0xFFFFFF) << 16)
           | (0x89ULL << 40)
           | (((limit >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    gdtr_t gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt
    };

    gdt_flush(&gdtr);
}
