BITS 64

section .text
global isr_breakpoint
extern breakpoint_seen

isr_breakpoint:
    mov byte [rel breakpoint_seen], 1
    iretq
