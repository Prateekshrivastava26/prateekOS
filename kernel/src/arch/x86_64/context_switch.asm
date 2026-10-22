BITS 64

section .text
global context_switch

; void context_switch(uint64_t **old_stack, uint64_t *new_stack)
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
