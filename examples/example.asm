SECTION .data
mem8: db 210
SECTION .text
global _start
_start:
mov rbp , rsp
mov ax, 0xAABB
mov bx, 0xCCDD
push word 3
push bx
push ax

;mov ax, -20
;movzx bx, byte [mem8]
;add ax, bx

call sum

mov rax, 60
xor rdi, rdi
syscall


sum:
 mov al,10
 ret
