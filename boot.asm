; boot.asm
[BITS 16]
[ORG 0x7C00]

start:
    ; --- 关键修正：初始化段寄存器 ---
    ; 必须确保 DS=0，因为 disk_packet 的偏移是基于 ORG 0x7C00 (DS=0) 计算的
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00  ; 设置栈指针往下长，避开引导代码
    ; -----------------------------

    ; 1. VGA 设置 (Mode 13h)
    mov ax, 0x0013
    int 0x10

    ; 调试：把屏幕涂成蓝色，确认引导程序在跑
    mov ax, 0xA000
    mov es, ax
    xor di, di
    mov al, 1       ; 蓝色
    mov cx, 320*200
    rep stosb

    ; 2. LBA 扩展读取内核
    ; 使用定义在下面的 disk_packet 结构体
    mov ah, 0x42
    mov dl, 0x80         ; 硬盘 ID
    mov si, disk_packet  ; DS:SI 指向数据包
    int 0x13
    jc disk_error        ; 如果 CF=1 则跳转报错

    mov ah, 0x42
    mov dl, 0x80
    mov si, disk_packet_2
    int 0x13
    jc disk_error

    ; 3. 关中断
    cli

    ; 4. 加载 GDT
    lgdt [gdt_descriptor]

    ; 5. 开启保护模式
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    ; 6. 远跳转，刷新流水线进入 32 位模式
    jmp 0x08:init_pm

; --- 错误处理 ---
disk_error:
    mov ah, 0x0e
    mov al, 'E'      ; 屏幕打印 'E'
    int 0x10
    jmp $            ; 死循环

; --- LBA 磁盘读取数据包 (Disk Address Packet) ---
; 必须确保这个结构体是 4字节对齐的（虽然这里不严格要求，但好习惯）
align 4
disk_packet:
    db 0x10
    db 0
    dw 127        ; 第一段：0x10000 -> 0x1FE00
    dw 0x0000     ; 偏移 (Offset) = 0
    dw 0x1000     ; 段 (Segment)  = 0x1000
    ; 物理地址 = 0x1000 * 16 + 0x0000 = 0x10000 (64KB)
    
    dd 1          ; 起始 LBA
    dd 0

disk_packet_2:
    db 0x10
    db 0
    dw 127        ; 第二段：0x1FE00 -> 0x2FC00，总计可载入约 127KB 内核
    dw 0x0000
    dw 0x1FE0
    dd 128
    dd 0
; --- 32位 保护模式入口 ---
[BITS 32]
init_pm:
    ; 初始化数据段寄存器
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov ebp, 0x9F000 ; 设置栈顶，保持在 VGA 显存(0xA0000)下方
    mov esp, ebp
    
    ; 调试：在 Mode 13h 显存显示一个白块，确认进入了保护模式
    mov edi, 0xA0000
    mov al, 15      ; 白色
    mov ecx, 320*10 ; 顶部的10行
    rep stosb
    
    ; 跳转到 C 内核入口
    call 0x10000
    
    ; 如果内核返回，显示 'R'
    mov byte [0xA0002], 'R'
    jmp $ ; 如果内核返回，死循环

; --- GDT 定义 (放在最后是为了不打断代码流) ---
gdt_start:
    dd 0x0, 0x0             ; Null Descriptor
gdt_code:
    dw 0xFFFF, 0x0000       ; Code Segment (0x08)
    db 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    dw 0xFFFF, 0x0000       ; Data Segment (0x10)
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; 填充 MBR 到 512 字节
times 510 - ($ - $$) db 0
dw 0xAA55
