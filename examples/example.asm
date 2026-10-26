SECTION .data
mem8: db -100
SECTION .text
global _start
_start:
mov al, -20
inc al
add al, -120
add al, [mem8]
mov ah, al
add al, ah
xor ah,ah
adc ah,0
mov rax, 60
xor rdi, rdi
syscall