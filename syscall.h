#ifndef SYSCALL_H
#define SYSCALL_H

// 定义系统调用号
#define SYS_EXIT        0
#define SYS_PRINT       1
#define SYS_READ_FILE   2
#define SYS_DRAW_RECT   3
#define SYS_SLEEP       4
#define SYS_SET_SANDBOX 5
#define SYS_GET_KEY         6
#define SYS_DRAW_TEXT       7
#define SYS_WIN_CREATE      8
#define SYS_WIN_SET_TITLE   9
#define SYS_WIN_IS_FOCUSED  10
#define SYS_WIN_GET_EVENT   11
#define SYS_FS_LIST         12
#define SYS_LAUNCH_TSK      13

// 窗口事件位
#define WIN_EVENT_FOCUS_CHANGED 0x1
#define WIN_EVENT_KEY_READY     0x2

// 寄存器结构体
// 注意：这必须与 kernel_entry.asm 中 isr80 里的 push 顺序严格对应
// 栈的生长方向是高地址->低地址，所以结构体成员顺序是反过来的
typedef struct {
    unsigned int gs, fs, es, ds;      // push ds, es, fs, gs
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax; // pusha
    unsigned int int_no, err_code;    // push 0x80, push 0
    unsigned int eip, cs, eflags, useresp, ss; // CPU 自动压入
} Registers;

// 初始化函数
void syscall_init();

// 沙箱级别
typedef enum {
    SANDBOX_NONE,    // 无沙箱，完全访问
    SANDBOX_BASIC,   // 基本沙箱，限制文件访问
    SANDBOX_STRICT   // 严格沙箱，只允许绘图和退出
} SandboxLevel;

#endif
