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
#include "klog.h"
#include "net.h"

// 声明外部函数
extern void init_timer(int freq);
extern unsigned char __bss_start;
extern unsigned char __bss_end;

// --- 鼠标状态记录 ---
int mouse_left_prev = 0;
static int desktop_wallpaper_style = 0;
static int start_page_enabled = 1;

void kernel_set_wallpaper_style(int style) {
    if (style < 0) style = 0;
    if (style > 2) style = 2;
    if (desktop_wallpaper_style == style) return;
    desktop_wallpaper_style = style;
    video_request_redraw();
}

void kernel_set_start_page_enabled(int enabled) {
    int value = enabled ? 1 : 0;
    if (start_page_enabled == value) return;
    start_page_enabled = value;
    video_request_redraw();
}

static int parse_ipv4_local(const char* s, unsigned char out[4]) {
    int idx = 0;
    int pos = 0;

    if (!s || !out) return 0;

    while (idx < 4) {
        int value = 0;
        int digits = 0;

        if (s[pos] < '0' || s[pos] > '9') return 0;
        while (s[pos] >= '0' && s[pos] <= '9') {
            value = value * 10 + (s[pos] - '0');
            if (value > 255) return 0;
            pos++;
            digits++;
        }
        if (digits <= 0) return 0;
        out[idx++] = (unsigned char)value;

        if (idx == 4) break;
        if (s[pos] != '.') return 0;
        pos++;
    }

    return s[pos] == '\0';
}

void kernel_reload_system_config(void) {
    SystemFile file;
    char buf[512];
    int n;
    int pos = 0;
    int screen_w = 0;
    int screen_h = 0;

    video_get_resolution(&screen_w, &screen_h);

    if (!sys_file_open("system/config.rtsk", &file)) return;
    if (file.size == 0 || file.size >= sizeof(buf)) return;

    n = fs_read_file("system/config.rtsk", buf);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    buf[n] = '\0';

    while (pos < n) {
        char line[96];
        int li = 0;

        while (pos < n && (buf[pos] == '\r' || buf[pos] == '\n')) pos++;
        if (pos >= n) break;

        while (pos < n && buf[pos] != '\r' && buf[pos] != '\n' && li < (int)sizeof(line) - 1) {
            line[li++] = buf[pos++];
        }
        while (pos < n && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        line[li] = '\0';

        if (!line[0] || line[0] == '#') continue;

        if (strncmp(line, "wallpaper=", 10) == 0) {
            const char* value = line + 10;
            if (strcmp(value, "ocean") == 0) {
                kernel_set_wallpaper_style(1);
            } else if (strcmp(value, "forest") == 0) {
                kernel_set_wallpaper_style(2);
            } else {
                kernel_set_wallpaper_style(0);
            }
        } else if (strncmp(line, "start_page=", 11) == 0) {
            const char* value = line + 11;
            kernel_set_start_page_enabled(strcmp(value, "disabled") != 0 && strcmp(value, "off") != 0);
        } else if (strncmp(line, "screen_w=", 9) == 0) {
            int value = 0;
            const char* p = line + 9;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                p++;
            }
            if (value > 0) screen_w = value;
        } else if (strncmp(line, "screen_h=", 9) == 0) {
            int value = 0;
            const char* p = line + 9;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                p++;
            }
            if (value > 0) screen_h = value;
        } else if (strncmp(line, "local_ip=", 9) == 0) {
            unsigned char ip[4];
            if (parse_ipv4_local(line + 9, ip)) {
                net_set_local_ip(ip[0], ip[1], ip[2], ip[3]);
            }
        } else if (strncmp(line, "gateway=", 8) == 0) {
            unsigned char ip[4];
            if (parse_ipv4_local(line + 8, ip)) {
                net_set_gateway(ip[0], ip[1], ip[2], ip[3]);
            }
        } else if (strncmp(line, "dns=", 4) == 0) {
            unsigned char ip[4];
            if (parse_ipv4_local(line + 4, ip)) {
                net_set_dns_server(ip[0], ip[1], ip[2], ip[3]);
            }
        }
    }

    video_set_resolution(screen_w, screen_h);
}

static void draw_desktop_background(void) {
    if (desktop_wallpaper_style == 1) {
        draw_rect(0, 0, SCREEN_WIDTH, 120, C_LIGHT_BLUE);
        draw_rect(0, 120, SCREEN_WIDTH, 80, C_BLUE);
        draw_rect(18, 18, 32, 12, C_WHITE);
    } else if (desktop_wallpaper_style == 2) {
        draw_rect(0, 0, SCREEN_WIDTH, 126, C_LIGHT_GREEN);
        draw_rect(0, 126, SCREEN_WIDTH, 74, C_GREEN);
        draw_rect(0, 150, SCREEN_WIDTH, 8, C_BROWN);
    } else {
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_CYAN);
    }
}

static int is_start_window(Window* w) {
    return (w && w->title && strcmp(w->title, "start.tsk") == 0);
}

static int is_start_open(void) {
    int count = win_get_count();
    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);
        if (is_start_window(w)) return 1;
    }
    return 0;
}

// --- 绘制任务栏 (Taskbar) ---
void draw_taskbar() {
    // Start 页面打开时，整个任务栏隐藏
    if (is_start_open()) return;

    // 1. 任务栏背景 (灰色)
    draw_rect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, C_LIGHT_GRAY);
    
    // 2. 开始按钮 (Start Button)
    draw_rect(0, SCREEN_HEIGHT - 20, 50, 20, start_page_enabled ? C_DARK_GRAY : C_RED);
    draw_string(8, SCREEN_HEIGHT - 15, start_page_enabled ? "TSK" : "OFF", C_WHITE);

    // 3. 遍历所有窗口，绘制任务栏按钮
    int count = win_get_count();
    int btn_width = 60;
    int start_x = 55;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);
        if (!w || is_start_window(w)) continue;

        draw_rect(start_x, SCREEN_HEIGHT - 18, btn_width, 16, C_WHITE);
        draw_string(start_x + 2, SCREEN_HEIGHT - 14, w->title, C_BLACK);
        
        // 只给当前激活窗口画蓝色下划线
        if (w == win_get_at_layer(count - 1)) {
            draw_rect(start_x, SCREEN_HEIGHT - 2, btn_width, 2, C_BLUE);
        }

        start_x += btn_width + 2;
    }
}

// --- 处理点击任务栏的逻辑 ---
int handle_taskbar_click(int mx, int my) {
    // Start 页面打开时，任务栏点击逻辑完全关闭
    if (is_start_open()) return 0;

    if (my < SCREEN_HEIGHT - 20) return 0;

    // 点击开始按钮 → 启动/聚焦 system/start.tsk
    if (mx < 50) {
        if (!start_page_enabled) return 1;
        klog_write("start click");
        if (!console_launch_tsk("system/start.tsk")) {
            klog_write("start launch failed");
            kpanic("cannot launch system/start.tsk");
        }
        return 1;
    }

    int count = win_get_count();
    int btn_width = 60;
    int start_x = 55;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);
        if (!w || is_start_window(w)) continue;

        if (mx >= start_x && mx < start_x + btn_width) {
            if (w) {
                win_bring_to_front(w);
            }
            return 1;
        }
        start_x += btn_width + 2;
    }

    return 0;
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
                if (handle_taskbar_click(mouse_x, mouse_y)) {
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
            draw_desktop_background();
            win_draw_all();

            draw_taskbar();
            draw_mouse();
            video_swap_buffer();
        }

        // 不使用 hlt，或者小心使用，确保中断能唤醒
        // __asm__ volatile("hlt");
    }
}
