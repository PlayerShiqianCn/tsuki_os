#ifndef PROCESS_H
#define PROCESS_H

#include "window.h"

// 进程状态
typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_DEAD
} ProcessState;

// 进程控制块 (PCB)
typedef struct Process {
    int pid;
    char name[32];
    unsigned int esp;       // 内核栈指针 (切换时的保存点)
    unsigned int stack_base;// 栈底地址 (用于回收内存)
    ProcessState state;
    int sandbox_level;
    int focus_state_cache;
    Window* win;            // 绑定的窗口
    struct Process* next;   // 链表
} Process;

// 全局当前进程指针
extern Process* current_process;

void process_init();
void process_create(void (*entry_point)(), const char* name, Window* win);
void process_exit();
Process* process_find_by_window(Window* win);
Process* process_find_by_name(const char* name);
int process_has_live_user_process(void);

// 调度函数：返回下一个进程的栈指针
// 如果不需要切换，返回当前的 esp
unsigned int process_schedule(unsigned int current_esp);

#endif
