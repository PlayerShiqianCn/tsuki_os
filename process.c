#include "process.h"
#include "heap.h"
#include "utils.h"
#include "console.h"

Process* current_process = 0;
static Process* process_list = 0;
static int next_pid = 0;

// 定义初始栈的大小
#define STACK_SIZE 4096

void process_init() {
    // 创建内核闲置进程 (PID 0)
    // 它代表了 kernel.c 中的 main 循环
    Process* kernel_proc = (Process*)malloc(sizeof(Process));
    kernel_proc->pid = next_pid++;
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->stack_base = 0; // 内核栈不需要我们释放
    kernel_proc->esp = 0;        // 当前正在运行，ESP 在 CPU 寄存器里，暂存 0
    kernel_proc->win = 0;
    kernel_proc->next = 0;

    process_list = kernel_proc;
    current_process = kernel_proc;
    
    // console_write("Process system initialized.\n");
}

void process_create(void (*entry_point)(), const char* name, Window* win) {
    Process* new_proc = (Process*)malloc(sizeof(Process));
    new_proc->pid = next_pid++;
    new_proc->state = PROCESS_READY;
    new_proc->win = win;
    
    // 分配栈空间
    unsigned int stack = (unsigned int)malloc(STACK_SIZE);
    new_proc->stack_base = stack;
    
    // 初始化栈内容，模拟中断现场
    // 栈是从高地址向低地址增长的
    unsigned int* top = (unsigned int*)(stack + STACK_SIZE);

    // 1. 手动构建中断返回栈帧 (IRET 需要弹出 EIP, CS, EFLAGS)
    *(--top) = 0x202;           // EFLAGS (IF=1, 中断开启)
    *(--top) = 0x08;            // CS (内核代码段)
    *(--top) = (unsigned int)entry_point; // EIP

    // 2. pusha (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    *(--top) = 0; // EAX
    *(--top) = 0; // ECX
    *(--top) = 0; // EDX
    *(--top) = 0; // EBX
    *(--top) = 0; // ESP (pusha 忽略)
    *(--top) = 0; // EBP
    *(--top) = 0; // ESI
    *(--top) = 0; // EDI

    // 3. 段寄存器 (GS, FS, ES, DS)
    *(--top) = 0x10; // DS
    *(--top) = 0x10; // ES
    *(--top) = 0x10; // FS
    *(--top) = 0x10; // GS

    // 保存最终的栈顶指针
    new_proc->esp = (unsigned int)top;

    // 加入链表 (简单的轮转，加到头部)
    new_proc->next = process_list;
    process_list = new_proc;

    // console_write("Created process: ");
    // console_write((char*)name);
    // console_write("\n");
}

void process_exit() {
    // 简单的退出逻辑：标记为 DEAD，等待调度器回收或者重用
    // 这里我们简单地把状态设为 DEAD，调度器会跳过它
    // 实际 OS 需要回收资源
    if (current_process->pid == 0) return; // 内核进程不能退出

    current_process->state = PROCESS_DEAD;
    
    // 强制触发一次调度 (通过软中断或者等待下一次时钟)
    // 这里我们等待下一次时钟中断自然切换
    while(1) __asm__ volatile("hlt");
}

// 调度器：Round Robin
unsigned int process_schedule(unsigned int current_esp) {
    if (!current_process) return current_esp;

    // 1. 保存当前进程的 ESP
    current_process->esp = current_esp;

    // 2. 选择下一个 READY 的进程
    Process* next = current_process->next;
    int looped = 0;

    while (1) {
        if (!next) {
            next = process_list; // 回到链表头
            looped = 1;
        }

        // 如果转了一圈回到自己，且自己是 DEAD，说明系统死锁了？
        // 不，PID 0 (Kernel) 永远是 RUNNING 的，所以至少会选中 PID 0
        if (next == current_process && looped && next->state == PROCESS_DEAD) {
             // 紧急情况，不应该发生
             return current_esp;
        }

        if (next->state == PROCESS_READY || next->state == PROCESS_RUNNING) {
            break;
        }
        next = next->next;
    }

    // 3. 切换上下文
    if (next != current_process) {
        current_process = next;
        // 绑定当前窗口上下文 (用于 syscall)
        // 现在 syscall.c 中没有全局变量了，直接通过 current_process->win 获取
    }

    return current_process->esp;
}