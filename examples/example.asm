SECTION .data
mem8: db -100
SECTION .text
global _start
_start:
mov rax, 0x123456789ABCDEF0
mov [rsp-8], rax
mov [rsp-16], rax
mov al, -20
inc al
mov ah, al
add al, ah
xor ah,ah
adc ah,0

call sum

mov rax, 60
xor rdi, rdi
syscall


sum: 
 mov al,10
 ret