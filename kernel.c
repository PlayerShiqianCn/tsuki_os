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

typedef struct {
    int x;
    int y;
    int w;
    int h;
    unsigned char color;
    char* label;
    char* filename;
} StartTile;

static const StartTile start_tiles[] = {
    {12, 58, 90, 60, C_GREEN, "Terminal", "terminal.tsk"},
    {114, 58, 90, 60, C_BLUE, "WinMgr", "wm.tsk"},
    {216, 58, 90, 60, C_RED, "StartUI", "start.tsk"},
    {12, 124, 90, 52, C_YELLOW, "Demo", "app.tsk"},
};

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
    draw_string(8, SCREEN_HEIGHT - 15, "TSK", C_WHITE);

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
    draw_string(20, 20, "TSK Apps", C_WHITE);

    int tile_count = sizeof(start_tiles) / sizeof(start_tiles[0]);
    for (int i = 0; i < tile_count; i++) {
        const StartTile* tile = &start_tiles[i];
        draw_rect(tile->x, tile->y, tile->w, tile->h, tile->color);
        draw_string(tile->x + 8, tile->y + 18, tile->label, C_WHITE);
        draw_string(tile->x + 8, tile->y + 38, tile->filename, C_WHITE);
    }
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
    int tile_count = sizeof(start_tiles) / sizeof(start_tiles[0]);
    for (int i = 0; i < tile_count; i++) {
        const StartTile* tile = &start_tiles[i];
        if (mx >= tile->x && mx <= tile->x + tile->w &&
            my >= tile->y && my <= tile->y + tile->h) {
            is_start_menu_open = 0;
            if (!console_launch_tsk(tile->filename)) {
                console_write("Launch failed: ");
                console_write(tile->filename);
                console_write("\n");
            }
            return;
        }
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
        // 主循环主动轮询输入，避免仅依赖 IRQ 导致的“假死”。
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

        // 键盘处理：
        // 1) 开始菜单打开时，键盘归内核处理（Esc 关闭）
        // 2) 焦点在内核窗口（如 console）时，键盘给 console
        // 3) 焦点在 tsk 进程窗口时，不在这里消费，交给 SYS_GET_KEY
        Window* focused_win = win_get_focused();
        Process* focused_proc = process_find_by_window(focused_win);

        if (is_start_menu_open) {
            char k = ps2_getchar();
            if (k == 27) {
                is_start_menu_open = 0;
                needs_redraw = 1;
            }
        } else if (!focused_proc) {
            char k = ps2_getchar();
            if (k) {
                console_handle_key(k);
                needs_redraw = 1;
            }
        }

        // 2. 渲染 (Rendering)
        // 在多任务环境下持续刷新，保证窗口关闭/退出后不会残影。
        needs_redraw = 1;
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
