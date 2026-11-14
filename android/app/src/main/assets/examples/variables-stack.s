// Real ARM64: variable → register → stack → variable
.data
value:  .quad 10
result: .quad 0
.text
.global _start
_start:
    adr x9, value
    ldr x0, [x9]           // Read value = 10
    add x0, x0, #5         // X0 = 15
    str x0, [x9]           // Write value = 15
    mov x1, x0            // Copy to X1
    str x1, [sp, #-16]!   // Push, keep SP aligned
    ldr x2, [sp]          // Read the stack
    ldr x3, [sp], #16     // Pop, restore SP
    adr x9, result
    str x3, [x9]          // Write result = 15
    nop
