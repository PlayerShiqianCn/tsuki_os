[bits 32]

extern main
extern exit
extern image_app_stack

global _start

_start:
    mov esp, image_app_stack + 4096
    call main
    call exit
    jmp $
