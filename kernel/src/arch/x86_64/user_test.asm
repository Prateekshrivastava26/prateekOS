BITS 64

section .rodata
align 16
global user_program_start
global user_program_end

user_program_start:
    mov eax, 1
    lea rdi, [rel user_message]
    int 0x80

    mov eax, 2
    int 0x80

    ud2

user_message:
    db "[user] Hello from ring 3!", 10, 0

user_program_end:
