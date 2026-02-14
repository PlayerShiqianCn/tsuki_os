; kernel_entry.asm
[bits 32]
[extern main]
[EXTERN timer_tick_and_schedule]
[EXTERN keyboard_handler_isr]
[EXTERN mouse_handler_isr]

global _start
_start:
    ; 1. 【绝对关键】立刻关闭中断！
    cli

    ; 2. 【绝对关键】设置栈指针
    mov esp, 0x90000
    mov ebp, esp

    ; 这里的 0x10000 是由 linker.ld 决定的
    call main
    jmp $

; --- 全局导出符号 ---
global irq0_handler_stub
global irq1_handler_stub
global irq2_handler_stub
global irq3_handler_stub
global irq4_handler_stub
global irq5_handler_stub
global irq6_handler_stub
global irq7_handler_stub
global irq8_handler_stub
global irq9_handler_stub
global irq10_handler_stub
global irq11_handler_stub
global irq12_handler_stub
global irq13_handler_stub
global irq14_handler_stub
global irq15_handler_stub
global isr_ignore_stub
global isr_err_stub

; --- 时钟中断跳板 (IRQ0) ---
; 支持多任务调度
irq0_handler_stub:
    ; 1. 保存上下文 (Context)
    pusha           ; EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    push ds
    push es
    push fs
    push gs

    ; 2. 切换到内核数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 3. 发送 EOI (End of Interrupt)
    mov al, 0x20
    out 0x20, al

    ; 4. 调用 C 调度器
    ; unsigned int timer_tick_and_schedule(unsigned int current_esp)
    push esp        ; 传入当前栈顶指针
    call timer_tick_and_schedule
    mov esp, eax    ; 更新栈顶指针 (这就完成了任务切换！)

    ; 5. 恢复上下文
    pop gs
    pop fs
    pop es
    pop ds
    popa            ; 恢复通用寄存器
    iret            ; 返回 (弹出 EIP, CS, EFLAGS)

; --- 键盘中断跳板 (IRQ1) ---
irq1_handler_stub:
    pusha
    cld
    call keyboard_handler_isr
    mov al, 0x20
    out 0x20, al
    popa
    iret

; --- IRQ2 ---
irq2_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ3 ---
irq3_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ4 ---
irq4_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ5 ---
irq5_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ6 ---
irq6_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ7 ---
irq7_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- IRQ8 ---
irq8_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ9 ---
irq9_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ10 ---
irq10_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ11 ---
irq11_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ12 ---
irq12_handler_stub:
    pusha
    cld
    call mouse_handler_isr
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ13 ---
irq13_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ14 ---
irq14_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

; --- IRQ15 ---
irq15_handler_stub:
    pusha
    cld
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iret

; --- 错误异常处理 ---
isr_err_stub:
    cli
    pusha
    mov edi, 0xA0000
    mov al, 4 ; 红色
    mov ecx, 320*200
    rep stosb
    jmp $

isr_ignore_stub:
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    iret

global isr80
extern syscall_handler

isr80:
    cli
    push byte 0
    push byte 0x80
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    sti
    iret