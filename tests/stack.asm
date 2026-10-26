section .text
global _start
_start:
    mov r12, rsp
    mov rax, 0x1122334455667788
    push rax
    mov byte [rsp], 0xaa
    pop rbx
    mov rsp, 1
    mov rsp, r12
    mov rax, 60
    xor rdi, rdi
    syscall
