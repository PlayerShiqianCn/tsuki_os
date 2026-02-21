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

void draw_text(int x, int y, const char* str, int color) {
    _syscall5(SYS_DRAW_TEXT, x, y, (int)str, color, 0);
}

int get_key(void) {
    return _syscall3(SYS_GET_KEY, 0, 0, 0);
}

void set_sandbox(int level) {
    _syscall3(SYS_SET_SANDBOX, level, 0, 0);
}

int win_create(int x, int y, int w, int h, const char* title) {
    return _syscall5(SYS_WIN_CREATE, x, y, w, h, (int)title);
}

int win_set_title(const char* title) {
    return _syscall3(SYS_WIN_SET_TITLE, (int)title, 0, 0);
}

int win_is_focused(void) {
    return _syscall3(SYS_WIN_IS_FOCUSED, 0, 0, 0);
}

int win_get_event(void) {
    return _syscall3(SYS_WIN_GET_EVENT, 0, 0, 0);
}

int list_files(char* buffer, int max_len) {
    return _syscall3(SYS_FS_LIST, (int)buffer, max_len, 0);
}

int launch_tsk(const char* filename) {
    return _syscall3(SYS_LAUNCH_TSK, (int)filename, 0, 0);
}
