#include "disk.h" // for outb
#include "console.h"
#include "process.h"

static unsigned int tick = 0;

void init_timer(unsigned int frequency) {
    // 设置 PIT (Programmable Interval Timer)
    unsigned int divisor = 1193180 / frequency;

    // 命令字：0x36 (Channel 0, Lobyte/Hibyte, Mode 3)
    outb(0x43, 0x36);
    
    // 拆分频率除数
    unsigned char l = (unsigned char)(divisor & 0xFF);
    unsigned char h = (unsigned char)( (divisor>>8) & 0xFF );

    outb(0x40, l);
    outb(0x40, h);

    // 【关键】开启 IRQ0 (主片 mask 的第 0 位清零)
    unsigned char mask = inb(0x21);
    mask &= ~0x01; // Clear bit 0 (IRQ0)
    outb(0x21, mask);
}

// 这个函数由汇编 irq0_handler_stub 调用
// 它返回一个新的栈指针 (如果发生调度)
unsigned int timer_tick_and_schedule(unsigned int current_esp) {
    tick++;
    process_on_timer_tick();

    // 每个 tick 都调度，保证用户进程能够及时获得时间片
    return process_schedule(current_esp);
}
