BITS 64

section .text
global enter_user_mode
global user_mode_exit
global isr_syscall
global user_kernel_rsp
extern syscall_dispatch

section .bss
align 8
user_kernel_rsp: resq 1

section .text

; C signature: enter_user_mode(user_rip, user_rsp)
enter_user_mode:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rel user_kernel_rsp], rsp
    cli

    ; Set user data selectors before entering ring 3.
    mov ax, 0x1B
    mov ds, ax
    mov es, ax

    ; iretq frame: SS, RSP, RFLAGS, CS, RIP
    push qword 0x1B
    push rsi
    push qword 0x2       ; interrupts stay disabled during this demo
    push qword 0x23
    push rdi
    iretq

; Software interrupt 0x80 from ring 3.
isr_syscall:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov r12, rsp
    and rsp, -16

    ; Saved RAX is the syscall number; saved RDI is its argument.
    mov rdi, [r12 + 112]
    mov rsi, [r12 + 64]
    call syscall_dispatch

    mov rsp, r12
    cmp rax, 1
    je user_mode_exit

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

; Exit the demo and resume the kernel call to enter_user_mode().
user_mode_exit:
    cli
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rsp, [rel user_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    sti
    ret
