BITS 64

global _start

section .text
_start:
    mov eax, 1              ; PRATEEKOS write syscall
    lea rdi, [rel message]
    int 0x80

    mov eax, 2              ; PRATEEKOS exit syscall
    int 0x80

.hang:
    jmp .hang

section .rodata
message:
    db "[user ELF] Hello from a separate program!", 10, 0
