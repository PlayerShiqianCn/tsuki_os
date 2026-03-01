#include "process.h"
#include "mp.h"
#include "heap.h"
#include "utils.h"
#include "console.h"
#include "klog.h"

Process* current_process = 0;
static Process* process_list = 0;
static int next_pid = 0;
static unsigned int scheduler_tick_count = 0;

// 定义初始栈的大小
#define STACK_SIZE 12288
#define MAX_PROCESSES 32
#define PROCESS_TIME_SLICE_TICKS 4
#define CONTEXT_FRAME_WORDS 15

static Process process_pool[MAX_PROCESSES];
static unsigned char process_used[MAX_PROCESSES];
static unsigned char process_stacks[MAX_PROCESSES][STACK_SIZE];

static int process_context_is_valid(const Process* proc) {
    unsigned int stack_limit;
    unsigned int saved_eip;
    unsigned int saved_cs;
    unsigned int* frame;

    if (!proc) return 0;
    if (proc->pid == 0) return 1;
    if (!proc->stack_base || !proc->esp) return 0;

    stack_limit = proc->stack_base + STACK_SIZE;
    if ((proc->esp & 3u) != 0) return 0;
    if (proc->esp < proc->stack_base) return 0;
    if (proc->esp > stack_limit - CONTEXT_FRAME_WORDS * sizeof(unsigned int)) return 0;

    frame = (unsigned int*)proc->esp;
    saved_eip = frame[12];
    saved_cs = frame[13];

    if (saved_cs != 0x08) return 0;
    if (saved_eip >= MP_KERNEL_CODE_BASE && saved_eip < MP_APP_SLOT_BASE) return 1;
    if (proc->code_base != 0 && proc->code_limit > proc->code_base &&
        saved_eip >= proc->code_base && saved_eip < proc->code_limit) {
        return 1;
    }

    return 0;
}

static int is_runnable(const Process* proc) {
    if (!proc) return 0;
    return proc->state == PROCESS_READY || proc->state == PROCESS_RUNNING;
}

static void reset_time_slice(Process* proc) {
    if (!proc) return;
    proc->time_slice_remaining = PROCESS_TIME_SLICE_TICKS;
}

static Process* alloc_process_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_used[i]) {
            process_used[i] = 1;
            memset(&process_pool[i], 0, sizeof(Process));
            return &process_pool[i];
        }
    }
    return 0;
}

static void unlink_process(Process* proc) {
    Process* prev = 0;
    Process* p = process_list;

    if (!proc) return;

    while (p) {
        if (p == proc) {
            if (prev) prev->next = p->next;
            else process_list = p->next;
            return;
        }
        prev = p;
        p = p->next;
    }
}

static void free_process_slot(Process* proc) {
    if (!proc) return;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (&process_pool[i] == proc) {
            process_used[i] = 0;
            return;
        }
    }
}

static void reap_dead_processes(void) {
    Process* prev = 0;
    Process* p = process_list;

    while (p) {
        if (p != current_process && p->state == PROCESS_DEAD) {
            Process* dead = p;
            if (prev) prev->next = p->next;
            else process_list = p->next;
            p = p->next;
            free_process_slot(dead);
            continue;
        }
        prev = p;
        p = p->next;
    }
}

static unsigned int stack_base_for(Process* proc) {
    if (!proc) return 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (&process_pool[i] == proc) {
            return (unsigned int)&process_stacks[i][0];
        }
    }
    return 0;
}

static void wake_blocked_processes(void) {
    Process* p = process_list;

    while (p) {
        if (p->state == PROCESS_BLOCKED &&
            p->wake_tick != 0 &&
            scheduler_tick_count >= p->wake_tick) {
            p->wake_tick = 0;
            p->state = PROCESS_READY;
            reset_time_slice(p);
        }
        p = p->next;
    }
}

static Process* next_runnable_from(Process* start) {
    Process* p;

    if (!process_list) return 0;

    p = start ? start : process_list;
    while (p) {
        if (is_runnable(p)) return p;
        p = p->next;
    }

    p = process_list;
    while (p && p != start) {
        if (is_runnable(p)) return p;
        p = p->next;
    }

    return 0;
}

void process_init() {
    // 创建内核闲置进程 (PID 0)
    // 它代表了 kernel.c 中的 main 循环
    Process* kernel_proc = alloc_process_slot();
    if (!kernel_proc) return;
    kernel_proc->pid = next_pid++;
    strcpy(kernel_proc->name, "kernel");
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->stack_base = 0; // 内核栈不需要我们释放
    kernel_proc->code_base = 0;
    kernel_proc->code_limit = 0;
    kernel_proc->esp = 0;        // 当前正在运行，ESP 在 CPU 寄存器里，暂存 0
    kernel_proc->wake_tick = 0;
    kernel_proc->total_ticks = 0;
    kernel_proc->sandbox_level = 0;
    kernel_proc->focus_state_cache = -1;
    reset_time_slice(kernel_proc);
    kernel_proc->win = 0;
    kernel_proc->next = 0;

    process_list = kernel_proc;
    current_process = kernel_proc;
    
    // console_write("Process system initialized.\n");
}

int process_create(void (*entry_point)(), const char* name, Window* win,
                   unsigned int code_base, unsigned int code_limit) {
    Process* new_proc = alloc_process_slot();
    if (!new_proc) return 0;
    new_proc->pid = next_pid++;
    if (name) {
        int i = 0;
        for (; i < 31 && name[i]; i++) new_proc->name[i] = name[i];
        new_proc->name[i] = '\0';
    } else {
        new_proc->name[0] = '\0';
    }
    new_proc->state = PROCESS_READY;
    new_proc->code_base = code_base;
    new_proc->code_limit = code_limit;
    new_proc->wake_tick = 0;
    new_proc->total_ticks = 0;
    new_proc->sandbox_level = 0;
    new_proc->focus_state_cache = -1;
    reset_time_slice(new_proc);
    new_proc->mouse_click_x = 0;
    new_proc->mouse_click_y = 0;
    new_proc->has_mouse_event = 0;
    new_proc->win = win;
    
    // 分配栈空间
    unsigned int stack = stack_base_for(new_proc);
    if (!stack) {
        free_process_slot(new_proc);
        return 0;
    }
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
    return 1;
}

void process_exit() {
    // 简单的退出逻辑：标记为 DEAD，等待调度器回收或者重用
    // 这里我们简单地把状态设为 DEAD，调度器会跳过它
    // 实际 OS 需要回收资源
    if (current_process->pid == 0) return; // 内核进程不能退出

    if (current_process->win) {
        win_destroy(current_process->win);
        current_process->win = 0;
    }

    unlink_process(current_process);

    current_process->state = PROCESS_DEAD;

    // 不能返回到用户代码；_start 没有“exit 返回后”的安全路径。
    // 这里显式开中断并等待时钟中断把当前 DEAD 进程切走。
    // 注意：不能在这里立刻回收槽位，否则下一次 process_create 可能复用同一块
    // PCB/调度栈，而当前代码还在这块栈上运行，会把新进程现场踩坏。
    while (1) {
        __asm__ volatile("sti; hlt");
    }
}

void process_sleep(unsigned int ticks) {
    unsigned int wake_tick;

    if (!current_process || current_process->pid == 0) return;

    if (ticks == 0) ticks = 1;

    wake_tick = scheduler_tick_count + ticks;
    if (wake_tick < scheduler_tick_count) {
        wake_tick = 0xFFFFFFFFu;
    }

    current_process->wake_tick = wake_tick;
    current_process->state = PROCESS_BLOCKED;
    current_process->time_slice_remaining = 0;
}

void process_on_timer_tick(void) {
    scheduler_tick_count++;

    if (current_process && current_process->state == PROCESS_RUNNING) {
        current_process->total_ticks++;
        if (current_process->time_slice_remaining > 0) {
            current_process->time_slice_remaining--;
        }
    }

    wake_blocked_processes();
}

Process* process_find_by_window(Window* win) {
    if (!win) return 0;

    Process* p = process_list;
    while (p) {
        if (p->state != PROCESS_DEAD && p->win == win) {
            return p;
        }
        p = p->next;
    }
    return 0;
}

Process* process_find_by_name(const char* name) {
    if (!name) return 0;

    Process* p = process_list;
    while (p) {
        if (p->state != PROCESS_DEAD && strcmp(p->name, name) == 0) {
            return p;
        }
        p = p->next;
    }
    return 0;
}

int process_has_live_user_process(void) {
    Process* p = process_list;
    while (p) {
        if (p->pid != 0 && p->state != PROCESS_DEAD) {
            return 1;
        }
        p = p->next;
    }
    return 0;
}

// 调度器：Round Robin
unsigned int process_schedule(unsigned int current_esp) {
    Process* prev_process;
    Process* next;
    int keep_current;
    if (!current_process) return current_esp;

    prev_process = current_process;

    // 1. 保存当前进程的 ESP
    current_process->esp = current_esp;
    reap_dead_processes();

    next = next_runnable_from(prev_process->next);

    keep_current = 0;
    if (prev_process->state == PROCESS_RUNNING &&
        prev_process->time_slice_remaining > 0 &&
        (!next || next == prev_process)) {
        keep_current = 1;
    }
    if (prev_process->state == PROCESS_RUNNING && !keep_current) {
        prev_process->state = PROCESS_READY;
    }
    if (prev_process->state == PROCESS_RUNNING && keep_current) {
        return prev_process->esp;
    }

    // 2. 选择下一个 READY/RUNNING 的进程
    if (!next && is_runnable(prev_process)) {
        next = prev_process;
    }
    while (next && !process_context_is_valid(next)) {
        klog_write_pair("bad ctx ", next->name);
        if (next->win) {
            win_destroy(next->win);
            next->win = 0;
        }
        next->state = PROCESS_DEAD;
        if (next == prev_process) {
            next = 0;
            break;
        }
        next = next_runnable_from(next->next);
    }
    if (!next) {
        if (prev_process->state == PROCESS_DEAD) {
            free_process_slot(prev_process);
        }
        return current_esp;
    }

    // 3. 切换上下文
    if (next->state == PROCESS_READY) {
        next->state = PROCESS_RUNNING;
        if (next->time_slice_remaining == 0) {
            reset_time_slice(next);
        }
    }
    if (next != current_process) {
        current_process = next;
        // 绑定当前窗口上下文 (用于 syscall)
        // 现在 syscall.c 中没有全局变量了，直接通过 current_process->win 获取
    }

    reap_dead_processes();
    if (prev_process != current_process && prev_process->state == PROCESS_DEAD) {
        free_process_slot(prev_process);
    }

    return current_process->esp;
}
