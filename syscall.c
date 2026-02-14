#include "syscall.h"
#include "console.h"
#include "fs.h"
#include "video.h"
#include "heap.h"
#include "process.h"
#include "window.h" // 确保包含这个以获取 TITLE_BAR_HEIGHT

SandboxLevel current_sandbox_level = SANDBOX_NONE;

// 移除这里的硬编码定义，改用 window.h 中的定义
// #define TITLE_BAR_HEIGHT 20 
// #define BORDER_WIDTH 2

void sys_draw_rect_sandboxed(int x, int y, int w, int h, int color) {
    // 1. 无窗口模式：直接画到屏幕
    if (!current_process || !current_process->win) {
        draw_rect(x, y, w, h, (unsigned char)color);
        video_swap_buffer();
        return;
    }

    Window* win = current_process->win;

    // 2. 坐标转换与裁剪
    // offset 是为了让 App 感觉自己是在 (0,0) 画图，实际画在标题栏下方
    int offset_x = BORDER_WIDTH;
    int offset_y = TITLE_BAR_HEIGHT;
    
    // 计算客户区(Client Area)大小
    int client_w = win->w - (BORDER_WIDTH * 2);
    int client_h = win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH;

    // 裁剪逻辑 (Clipping)
    if (x >= client_w || y >= client_h) return; // 完全在外面
    if (x < 0) { w += x; x = 0; }               // 左/上越界处理
    if (y < 0) { h += y; y = 0; }
    if (x + w > client_w) w = client_w - x;     // 右/下越界处理
    if (y + h > client_h) h = client_h - y;
    if (w <= 0 || h <= 0) return;

    // 3. 【核心逻辑】写入 Window 缓冲区
    // 这里的 win_put_pixel 写入的是我们在 window.c 中统一的 buffer
    // 这样，当窗口被拖动或覆盖后重绘时，内容依然存在
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // 注意：这里传入的坐标是相对于 Window 左上角的
            win_put_pixel(win, x + j + offset_x, y + i + offset_y, (unsigned char)color);
        }
    }

    // 4. 【立即反馈】同时画到屏幕显存
    // 这可以让用户立刻看到结果，而不需要等待窗口系统的下一次刷新
    int screen_abs_x = win->x + x + offset_x;
    int screen_abs_y = win->y + y + offset_y;
    
    // 调用 video.c 的全局绘制函数
    draw_rect(screen_abs_x, screen_abs_y, w, h, (unsigned char)color);
    
    // 交换缓冲区显示出来
    video_swap_buffer();
}

void syscall_handler(Registers* regs) {
    // 沙箱安全检查
    if (current_sandbox_level != SANDBOX_NONE) {
        // 沙箱模式下只允许 退出 和 画图 (示例)
        if (regs->eax != SYS_EXIT && regs->eax != SYS_DRAW_RECT) {
            return; 
        }
    }

    switch (regs->eax) {
        case SYS_EXIT:
            process_exit();
            break;

        case SYS_PRINT:
            console_write((const char*)regs->ebx);
            break;

        case SYS_READ_FILE:
            regs->eax = fs_read_file((char*)regs->ebx, (void*)regs->ecx);
            break;

        case SYS_DRAW_RECT:
            // ebx=x, ecx=y, edx=w, esi=h, edi=color
            sys_draw_rect_sandboxed(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
            break;

        case SYS_SLEEP:
            // TODO: 实现 sleep
            break;

        case SYS_SET_SANDBOX:
            current_sandbox_level = (SandboxLevel)regs->ebx;
            break;
    }
}

void syscall_init() {}