// kernel.c - 优化版本
__asm__(".code32");

#include "video.h"
#include "window.h"
#include "ps2.h"
#include "utils.h"
#include "heap.h"
#include "console.h"
#include "fs.h"
#include "idt.h"
#include "process.h"

// --- 全局状态 ---
int is_start_menu_open = 0;

// 声明外部函数
extern void init_timer(int freq);

// --- 鼠标状态记录 ---
int mouse_left_prev = 0;

// --- 绘制任务栏 (Taskbar) ---
void draw_taskbar() {
    // 1. 任务栏背景 (灰色)
    draw_rect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, C_LIGHT_GRAY);
    
    // 2. 开始按钮 (Start Button)
    unsigned char start_btn_color = is_start_menu_open ? C_BLUE : C_DARK_GRAY;
    draw_rect(0, SCREEN_HEIGHT - 20, 50, 20, start_btn_color);
    draw_string(5, SCREEN_HEIGHT - 15, "Start", C_WHITE);

    if (is_start_menu_open) return;

    // 3. 遍历所有窗口，绘制任务栏按钮
    int count = win_get_count();
    int btn_width = 60;
    int start_x = 55;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);
        if (!w) continue;

        draw_rect(start_x, SCREEN_HEIGHT - 18, btn_width, 16, C_WHITE);
        draw_string(start_x + 2, SCREEN_HEIGHT - 14, w->title, C_BLACK);
        
        // 只给当前激活窗口画蓝色下划线
        if (w == win_get_at_layer(count - 1)) {
            draw_rect(start_x, SCREEN_HEIGHT - 2, btn_width, 2, C_BLUE);
        }

        start_x += btn_width + 2;
    }
}

// --- 绘制 Win8 风格全屏开始屏幕 ---
void draw_start_screen() {
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - 20, C_MAGENTA);
    draw_string(20, 20, "Start", C_WHITE);

    // Terminal 磁贴
    int tile_x = 40;
    int tile_y = 60;
    int tile_w = 80;
    int tile_h = 60;
    
    draw_rect(tile_x, tile_y, tile_w, tile_h, C_GREEN);
    draw_string(tile_x + 5, tile_y + 45, "Terminal", C_WHITE);
    draw_string(tile_x + 25, tile_y + 20, ">_", C_WHITE);
}

// --- 处理点击任务栏的逻辑 ---
int handle_taskbar_click(int mx, int my) {
    if (my < SCREEN_HEIGHT - 20) return 0;

    if (mx < 50) {
        is_start_menu_open = !is_start_menu_open;
        return 1;
    }

    if (is_start_menu_open) return 0;

    int count = win_get_count();
    int btn_width = 60;
    int start_x = 55;

    for (int i = 0; i < count; i++) {
        if (mx >= start_x && mx < start_x + btn_width) {
            Window* w = win_get_at_layer(i);
            if (w) {
                win_bring_to_front(w);
            }
            return 1;
        }
        start_x += btn_width + 2;
    }

    return 0;
}

// --- 处理开始屏幕的点击逻辑 ---
void handle_start_screen_click(int mx, int my) {
    int tile_x = 40;
    int tile_y = 60;
    int tile_w = 80;
    int tile_h = 60;

    if (mx >= tile_x && mx <= tile_x + tile_w &&
        my >= tile_y && my <= tile_y + tile_h) {
        is_start_menu_open = 0;
        console_init(); 
    }
}

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

    unsigned char* vga = (unsigned char*)0xA0000;
    for(int i=0; i<320*200; i++) vga[i] = 2; // 2 = Green

    video_init();
    video_swap_buffer();

    // 1. 初始化核心系统
    heap_init();
    init_idt();

    // 2. 初始化进程系统 (将自己标记为 PID 0)
    process_init();

    // 3. 初始化硬件
    init_timer(100); // 开启时钟，这里会开始尝试调度，但只有 PID 0，所以安全
    ps2_init();
    ps2_mouse_init();
    video_init();

    // 4. 初始化文件系统
    fs_init();

    // 5. 初始化窗口系统
    win_init();
    console_init();
        
    // 开启中断 (开始多任务调度)
    __asm__ volatile("sti");

    // 调试标志
    draw_rect(10, 10, 20, 20, C_WHITE); 
    video_swap_buffer();

    int needs_redraw = 1;

    // PID 0 (内核/GUI 线程) 的主循环
    while(1) {
        // 1. 处理输入 (Input)
        ps2_mouse_event_t evt;
        int has_event = 0;

        // 循环读取缓冲区直到清空
        while (ps2_get_mouse_event(&evt)) {
            has_event = 1;
            mouse_x += evt.dx;
            mouse_y += evt.dy;
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= SCREEN_WIDTH) mouse_x = SCREEN_WIDTH - 1;
            if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;
            
            int left_curr = evt.buttons & 1;
            int is_click = left_curr && !mouse_left_prev;

            if (is_click) {
                if (handle_taskbar_click(mouse_x, mouse_y)) {
                } else if (is_start_menu_open) {
                    handle_start_screen_click(mouse_x, mouse_y);
                } else {
                    win_handle_mouse(&evt, mouse_x, mouse_y);
                }
            } else {
                if (!is_start_menu_open) {
                     win_handle_mouse(&evt, mouse_x, mouse_y);
                }
            }

            mouse_left_prev = left_curr;
            needs_redraw = 1;
        }

        // 键盘处理
        char k = ps2_getchar();
        if (k) {
            if (!is_start_menu_open) {
                 console_handle_key(k);
            }
            if (k == 27 && is_start_menu_open) {
                is_start_menu_open = 0;
            }
            needs_redraw = 1;
        }

        // 2. 渲染 (Rendering)
        // 在多任务环境下，PID 0 负责 GUI 刷新
        if (needs_redraw) {
            if (is_start_menu_open) {
                draw_start_screen();
            } else {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_CYAN);
                win_draw_all();
            }

            draw_taskbar();
            draw_mouse();
            video_swap_buffer();
            
            needs_redraw = 0;
        }

        // 不使用 hlt，或者小心使用，确保中断能唤醒
        // __asm__ volatile("hlt");
    }
}
