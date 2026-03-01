[bits 32]

extern main
extern exit

global _start

_start:
    call main
    call exit
    jmp $
