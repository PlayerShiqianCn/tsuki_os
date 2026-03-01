[bits 32]

global app_entry
extern app_main

section .text
app_entry:
    call app_main

.hang:
    jmp .hang
