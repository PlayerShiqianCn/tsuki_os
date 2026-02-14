// app.c
#include "lib.h"

// 在 bss 段定义栈
unsigned char stack[8192];

void main();

// 强制裸函数，不生成 prologue
__attribute__((naked)) void _start() {
    // 手动设置 ESP 指向 stack 数组的末尾
    // 注意：这里需要以此函数的地址为基准
    __asm__ volatile (
        "movl %0, %%esp \n\t"
        "call main \n\t"
        "call exit \n\t"
        : 
        : "r" (stack + sizeof(stack)) 
        : "memory"
    );
}

void main() {
    // 1. 进入沙箱
    set_sandbox(1);

    // 2. 【测试】直接画一个巨大的白色背景
    // 颜色 15 = 白色。如果能看到这个，说明代码跑起来了！
    draw_rect(0, 0, 200, 150, 15);

    // 3. 画一个静止的红色方块 (颜色 4)
    draw_rect(50, 50, 30, 30, 4);

    // 4. 死循环保持
    while(1) {
        // 空循环
    }
}