BITS 64

section .text
global gdt_flush

gdt_flush:
    lgdt [rdi]

    ; Reload CS with the kernel code selector, 0x08.
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load the TSS selector, 0x28.
    mov ax, 0x28
    ltr ax
    ret
