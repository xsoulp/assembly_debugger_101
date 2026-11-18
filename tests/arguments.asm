section .text
global _start
_start:
    mov rax, [rsp]       ; argc, including the program name
    mov rbx, [rsp + 16]  ; argv[1]
    mov rdx, [rbx]       ; first eight bytes of argv[1]
    mov eax, 60
    xor edi, edi
    syscall
