section .data
value:  dq 10
result: dq 0
section .text
global _start
_start:
    mov rax, [value]   ; read a variable
    add rax, 5         ; 10 + 5 = 15
    mov [value], rax   ; write a variable
    mov rbx, rax       ; copy a register
    push rbx           ; write the stack
    mov rcx, [rsp]     ; read the stack
    pop rdx            ; restore from stack
    mov [result], rdx  ; save the result
    mov eax, 60
    xor edi, edi
    syscall
