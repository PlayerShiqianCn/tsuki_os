#include "idt.h"
#include "utils.h" // 假设里面有 memset, outb
#include "disk.h"
// 定义 256 个中断入口
struct idt_entry_struct idt_entries[256];
struct idt_ptr_struct   idt_ptr;

// 汇编中定义的处理函数入口 (ISR)
extern void isr_ignore_stub();
extern void isr_err_stub(); // 引入错误处理 stub
extern void isr_handler_stub();
extern void irq0_handler_stub(); // 时钟
extern void irq1_handler_stub(); // 键盘
extern void irq2_handler_stub();
extern void irq3_handler_stub();
extern void irq4_handler_stub();
extern void irq5_handler_stub();
extern void irq6_handler_stub();
extern void irq7_handler_stub();
extern void irq8_handler_stub();
extern void irq9_handler_stub();
extern void irq10_handler_stub();
extern void irq11_handler_stub();
extern void irq12_handler_stub();
extern void irq13_handler_stub();
extern void irq14_handler_stub();
extern void irq15_handler_stub();

// 设置单个 IDT 条目
static void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags   = flags;
}

// 简单的延时函数，等待 I/O 操作完成
static inline void io_wait() {
    outb(0x80, 0);
}

// 重新映射 PIC (8259A)
// 这是一个固定的魔法初始化序列
void pic_remap() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); // 主片 IRQ0 映射到 0x20
    outb(0xA1, 0x28); io_wait(); // 从片 IRQ8 映射到 0x28
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // 【关键】默认屏蔽所有中断，但在主片上开启 IRQ2 (从片级联位)
    // 如果不开启 IRQ2，从片（如鼠标 IRQ12）的中断永远传不到 CPU
    outb(0x21, 0xFB); // 1111 1011 (IRQ2 is bit 2, so ~0x04)
    outb(0xA1, 0xFF);
}

void init_idt() {
    idt_ptr.limit = sizeof(struct idt_entry_struct) * 256 - 1;
    idt_ptr.base  = (unsigned int)&idt_entries;

    // 1. 先把所有 256 个中断都设为 "忽略" (防止 Triple Fault)
    // 0x08 是内核代码段选择子，0x8E 是中断门属性
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (unsigned int)isr_ignore_stub, 0x08, 0x8E);
    }

    // 1.5 【关键】专门捕获 0-31 号 CPU 异常
    // 如果 PIC 没映射好，IRQ0(INT 8) 会触发这里。
    // 我们让它显示红屏，这样就能一眼看出是不是中断号冲突了。
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (unsigned int)isr_err_stub, 0x08, 0x8E);
    }

    // 2. 重新映射 PIC (非常重要，否则 IRQ0 会由 0x08 触发，与 Double Fault 冲突)
    pic_remap();

    // 3. 覆盖注册正确的处理函数
    idt_set_gate(32, (unsigned int)irq0_handler_stub, 0x08, 0x8E); // IRQ0 -> 时钟
    idt_set_gate(33, (unsigned int)irq1_handler_stub, 0x08, 0x8E); // IRQ1 -> 键盘
    idt_set_gate(34, (unsigned int)irq2_handler_stub, 0x08, 0x8E); // IRQ2 -> 从片级联
    idt_set_gate(35, (unsigned int)irq3_handler_stub, 0x08, 0x8E); // IRQ3 -> 串口2
    idt_set_gate(36, (unsigned int)irq4_handler_stub, 0x08, 0x8E); // IRQ4 -> 串口1
    idt_set_gate(37, (unsigned int)irq5_handler_stub, 0x08, 0x8E); // IRQ5 -> 并口2/声卡
    idt_set_gate(38, (unsigned int)irq6_handler_stub, 0x08, 0x8E); // IRQ6 -> 软盘
    idt_set_gate(39, (unsigned int)irq7_handler_stub, 0x08, 0x8E); // IRQ7 -> 并口1
    idt_set_gate(40, (unsigned int)irq8_handler_stub, 0x08, 0x8E); // IRQ8 -> RTC
    idt_set_gate(41, (unsigned int)irq9_handler_stub, 0x08, 0x8E); // IRQ9 -> 设备兼容
    idt_set_gate(42, (unsigned int)irq10_handler_stub, 0x08, 0x8E); // IRQ10 -> 网络/SCSI
    idt_set_gate(43, (unsigned int)irq11_handler_stub, 0x08, 0x8E); // IRQ11 -> USB/其他
    idt_set_gate(44, (unsigned int)irq12_handler_stub, 0x08, 0x8E); // IRQ12 -> PS/2 鼠标
    idt_set_gate(45, (unsigned int)irq13_handler_stub, 0x08, 0x8E); // IRQ13 -> FPU
    idt_set_gate(46, (unsigned int)irq14_handler_stub, 0x08, 0x8E); // IRQ14 -> IDE 主
    idt_set_gate(47, (unsigned int)irq15_handler_stub, 0x08, 0x8E); // IRQ15 -> IDE 从

    // 系统调用中断
    extern void isr80(); // 声明外部汇编标号
    idt_set_gate(0x80, (unsigned int)isr80, 0x08, 0x8E);

    // 4. 加载 IDT
    __asm__ volatile("lidt %0" : : "m" (idt_ptr));
}