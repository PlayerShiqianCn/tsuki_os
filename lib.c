// lib.c
#include "syscall.h" // 引用之前的头文件

// 3个参数的系统调用
static inline int _syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    // GCC 内联汇编
    // a=eax (num), b=ebx (arg1), c=ecx (arg2), d=edx (arg3)
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret) 
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3)
        : "memory"
    );
    return ret;
}

// 5个参数的系统调用 (用于画图)
static inline int _syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5) {
    int ret;
    // S=esi, D=edi
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), "S"(arg4), "D"(arg5)
        : "memory"
    );
    return ret;
}

// === 用户 API ===

void exit() {
    _syscall3(SYS_EXIT, 0, 0, 0);
}

void print(const char* str) {
    _syscall3(SYS_PRINT, (int)str, 0, 0);
}

int read_file(const char* filename, void* buffer) {
    return _syscall3(SYS_READ_FILE, (int)filename, (int)buffer, 0);
}

void draw_rect(int x, int y, int w, int h, int color) {
    _syscall5(SYS_DRAW_RECT, x, y, w, h, color);
}

void set_sandbox(int level) {
    _syscall3(SYS_SET_SANDBOX, level, 0, 0);
}