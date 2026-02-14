#ifndef IDT_H
#define IDT_H

#include "utils.h"

// IDT 条目结构 (必须严格按照 x86 规范)
struct idt_entry_struct {
    unsigned short base_low;  // 处理函数地址低16位
    unsigned short sel;       // 内核段选择子 (0x08)
    unsigned char  always0;   // 必须为0
    unsigned char  flags;     // 属性 (0x8E = 32位中断门)
    unsigned short base_high; // 处理函数地址高16位
} __attribute__((packed));

// IDT 指针 (传给 lidt 指令用的)
struct idt_ptr_struct {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

void init_idt();

#endif