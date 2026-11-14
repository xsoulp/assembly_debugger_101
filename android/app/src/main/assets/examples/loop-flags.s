// Watch X0 count down and Z become 1
.data
sum: .quad 0
.text
.global _start
_start:
    mov x0, #5
    mov x1, #0
again:
    add x1, x1, x0
    subs x0, x0, #1
    b.ne again
    adr x9, sum
    str x1, [x9]         // 5 + 4 + 3 + 2 + 1 = 15
    nop
