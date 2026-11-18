section .data
word_value: dw 0x83a5
word_out: dw 0
qword_value: dq 0xfedcba9876543210
qword_out: dq 0

section .text
global _start
_start:
    mov rbp, rsp
    mov rax, 0x1122334455667788
    push ax
    pop bx
    push rax
    pop rdx
    push word [word_value]
    pop word [word_out]
    push qword [qword_value]
    pop qword [qword_out]
    push rax
    push qword [rsp]
    pop rcx
    pop rdx
    push rax
    push rbx
    pop qword [rsp]
    pop rcx
    push rsp
    pop rsp
    push sp
    pop sp
    mov r15, 0xaabbccddeeff1234
    push r15
    pop r8
    push r8w
    pop r15w
    push ax
    pop bx
    push strict word -7
    pop ax
    push strict dword -1
    pop rcx
    push byte 5
    pop rdx
    mov rax, 60
    xor rdi, rdi
    syscall
