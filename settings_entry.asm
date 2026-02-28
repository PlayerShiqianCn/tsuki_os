[bits 32]

extern main
extern exit
extern settings_app_stack

global _start

_start:
    mov esp, settings_app_stack + 4096
    call main
    call exit
    jmp $
