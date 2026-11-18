section .rodata
prefix: db 'Hello '
prefix_length equ $ - prefix
euro_first: db 0xe2
euro_rest: db 0x82, 0xac
records: db 10, '^done', 10, '*stopped,reason="exited-normally"', 10
records_length equ $ - records
stderr_text: db 'stderr must not appear in stdout', 10
stderr_length equ $ - stderr_text
tail: db 'last bytes without newline'
tail_length equ $ - tail

%macro write_bytes 3
    mov eax, 1
    mov edi, %1
    lea rsi, [rel %2]
    mov edx, %3
    syscall
%endmacro

section .text
global _start
_start:
    write_bytes 1, prefix, prefix_length
    write_bytes 1, euro_first, 1
    write_bytes 1, euro_rest, 2
    write_bytes 1, records, records_length
    write_bytes 2, stderr_text, stderr_length
    write_bytes 1, tail, tail_length
    mov eax, 60
    xor edi, edi
    syscall
