section .data
v1: db 40
v2: db 100
v3: db 80
section .bss
resultado: resb 1
section .text
global _start
_start:
    xor ax, ax
    mov al, [v1]
    add al, [v2]
    add al, [v3]
    mov bl, 3
    div bl
    mov [resultado], al
    mov rax, 60
    xor rdi, rdi
    syscall
