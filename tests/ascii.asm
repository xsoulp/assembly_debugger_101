section .data
text: db 'A', 'B', ' ', 10, 0, 127, 255, '<'
section .text
global _start
_start:
    mov byte [text], 'Z'
    nop
    mov eax, 60
    xor edi, edi
    syscall
