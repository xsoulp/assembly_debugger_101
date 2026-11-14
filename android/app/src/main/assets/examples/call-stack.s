// BL/RET and a real 16-byte stack frame
.data
result: .quad 0
.text
.global _start
_start:
    mov x0, #10
    bl add_five
    adr x9, result
    str x0, [x9]
    b done
add_five:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x0, x0, #5
    ldp x29, x30, [sp], #16
    ret
done:
    nop
