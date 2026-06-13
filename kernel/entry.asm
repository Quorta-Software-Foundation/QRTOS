[BITS 64]

global _start
extern kernel_main

section .text
_start:
    cli
    xor rbp, rbp
    mov rsp, 0x7FF00
    call kernel_main
.halt:
    hlt
    jmp .halt
