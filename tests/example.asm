section .data
    value dq 10

section .text
global _start

_start:
    mov rax, [value]
    add rax, 5
    mov [value], rax

    mov rax, 60
    xor rdi, rdi
    syscall
