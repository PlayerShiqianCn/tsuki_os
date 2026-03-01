// kernel.c - 优化版本
__asm__(".code32");

#include "video.h"
#include "desktop.h"
#include "kernel_config.h"
#include "window.h"
#include "ps2.h"
#include "utils.h"
#include "heap.h"
#include "fs.h"
#include "idt.h"
#include "process.h"
#include "klog.h"
#include "net.h"

// 声明外部函数
extern void init_timer(int freq);
extern unsigned char __bss_start;
extern unsigned char __bss_end;

// --- 鼠标状态记录 ---
int mouse_left_prev = 0;

// 鼠标全局变量
int mouse_x = SCREEN_WIDTH / 2;
int mouse_y = SCREEN_HEIGHT / 2;

void draw_mouse() {
    // 画一个简单的箭头形状
    draw_rect(mouse_x, mouse_y, 1, 8, C_BLACK);           // 竖线
    draw_rect(mouse_x, mouse_y, 8, 1, C_BLACK);           // 横线
    draw_rect(mouse_x, mouse_y, 5, 5, C_WHITE);           // 填充
}

// 调试宏
#define DEBUG_DOT(color, offset) { unsigned char* v = (unsigned char*)0xA0000; v[offset] = color; }

void main() {
    // 关中断 (以防万一 Bootloader 开了)
    __asm__ volatile("cli");

    // 裸机环境没有 CRT，必须手动清零 .bss
    memset(&__bss_start, 0, (int)(&__bss_end - &__bss_start));
    klog_init();

    video_init();
    klog_write("video init");

    // 1. 初始化核心系统
    heap_init();
    klog_write("heap init");
    init_idt();
    klog_write("idt init");

    // 2. 初始化进程系统 (将自己标记为 PID 0)
    process_init();
    klog_write("process init");

    // 3. 初始化硬件
    init_timer(100); // 开启时钟，这里会开始尝试调度，但只有 PID 0，所以安全
    klog_write("timer init");
    ps2_init();
    klog_write("ps2 init");
    ps2_mouse_init();
    klog_write("mouse init");
    video_init();
    klog_write("video reset");

    // 4. 初始化文件系统
    fs_init();
    if (!fs_is_ready()) {
        klog_write("fs init failed");
        kpanic("filesystem init failed");
    }
    klog_write("fs init");

    kernel_reload_system_config();
    klog_write("config init");

    net_init();
    klog_write("net init");

    // 5. 初始化窗口系统
    win_init();
    klog_write("window init");
        
    // 开启中断 (开始多任务调度)
    __asm__ volatile("sti");

    video_request_redraw();

    // PID 0 (内核/GUI 线程) 的主循环
    while(1) {
        // 主循环主动轮询输入，避免仅依赖 IRQ 导致的"假死"。
        // 每次最多处理少量字节，防止长时间占用。
        for (int i = 0; i < 8; i++) {
            ps2_poll_inputs_once();
        }

        // 1. 处理输入 (Input)
        ps2_mouse_event_t evt;

        // 循环读取缓冲区直到清空
        while (ps2_get_mouse_event(&evt)) {
            mouse_x += evt.dx;
            mouse_y += evt.dy;
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= SCREEN_WIDTH) mouse_x = SCREEN_WIDTH - 1;
            if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;
            
            int left_curr = evt.buttons & 1;
            int is_click = left_curr && !mouse_left_prev;

            if (is_click) {
                if (desktop_handle_taskbar_click(mouse_x, mouse_y)) {
                    // 任务栏点击已处理
                } else {
                    win_handle_mouse(&evt, mouse_x, mouse_y);
                }
            } else {
                win_handle_mouse(&evt, mouse_x, mouse_y);
            }

            mouse_left_prev = left_curr;
            video_request_redraw();
        }

        // 键盘不再由内核消费，全部交给 tsk 进程通过 SYS_GET_KEY 获取

        // 2. 渲染 (Rendering)
        if (video_consume_redraw()) {
            desktop_draw_background();
            win_draw_all();

            desktop_draw_taskbar();
            draw_mouse();
            video_swap_buffer();
        }

        // 不使用 hlt，或者小心使用，确保中断能唤醒
        // __asm__ volatile("hlt");
    }
}
