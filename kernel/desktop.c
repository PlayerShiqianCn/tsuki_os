__asm__(".code32");

#include "desktop.h"

#include "console.h"
#include "kernel_config.h"
#include "klog.h"
#include "utils.h"
#include "video.h"
#include "window.h"

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

static int count_taskbar_windows(void) {
    int count = win_get_count();
    int visible = 0;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);

        if (!w || !w->visible || is_start_window(w)) continue;
        visible++;
    }

    return visible;
}

static int get_taskbar_button_width(int button_count) {
    int available = SCREEN_WIDTH - 55;
    int gap = 2;
    int width;

    if (button_count <= 0) return 0;

    width = (available - (button_count - 1) * gap) / button_count;
    if (width < 8) width = 8;
    if (width > 96) width = 96;
    return width;
}

static void format_taskbar_title(const char* title, char* out, int out_size, int btn_width) {
    const char* base = title ? title : "tsk";
    int src_len = 0;
    int copy_len;

    if (!out || out_size <= 0) return;

    for (const char* p = base; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    while (base[src_len]) src_len++;
    if (src_len > 4 &&
        base[src_len - 4] == '.' &&
        base[src_len - 3] == 't' &&
        base[src_len - 2] == 's' &&
        base[src_len - 1] == 'k') {
        src_len -= 4;
    }

    copy_len = (btn_width - 4) / 8;
    if (copy_len < 0) copy_len = 0;
    if (copy_len > out_size - 1) copy_len = out_size - 1;
    if (src_len < copy_len) copy_len = src_len;

    for (int i = 0; i < copy_len; i++) {
        out[i] = base[i];
    }
    out[copy_len] = '\0';
}

void desktop_draw_background(void) {
    switch (kernel_get_wallpaper_style()) {
        case 1:
            draw_rect(0, 0, SCREEN_WIDTH, 120, C_LIGHT_BLUE);
            draw_rect(0, 120, SCREEN_WIDTH, 80, C_BLUE);
            draw_rect(18, 18, 32, 12, C_WHITE);
            break;
        case 2:
            draw_rect(0, 0, SCREEN_WIDTH, 126, C_LIGHT_GREEN);
            draw_rect(0, 126, SCREEN_WIDTH, 74, C_GREEN);
            draw_rect(0, 150, SCREEN_WIDTH, 8, C_BROWN);
            break;
        default:
            draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_CYAN);
            break;
    }
}

void desktop_draw_taskbar(void) {
    int count;
    int visible_count;
    int btn_width;
    int start_x;

    if (is_start_open()) return;

    draw_rect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, C_LIGHT_GRAY);
    draw_rect(0, SCREEN_HEIGHT - 20, 50, 20, kernel_is_start_page_enabled() ? C_DARK_GRAY : C_RED);
    draw_string(8, SCREEN_HEIGHT - 15, kernel_is_start_page_enabled() ? "TSK" : "OFF", C_WHITE);

    count = win_get_count();
    visible_count = count_taskbar_windows();
    btn_width = get_taskbar_button_width(visible_count);
    start_x = 55;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);
        char label[16];

        if (!w || !w->visible || is_start_window(w)) continue;

        draw_rect(start_x, SCREEN_HEIGHT - 18, btn_width, 16, C_WHITE);
        format_taskbar_title(w->title, label, sizeof(label), btn_width);
        draw_string(start_x + 2, SCREEN_HEIGHT - 14, label, C_BLACK);

        if (w == win_get_at_layer(count - 1)) {
            draw_rect(start_x, SCREEN_HEIGHT - 2, btn_width, 2, C_BLUE);
        }

        start_x += btn_width + 2;
    }
}

int desktop_handle_taskbar_click(int mx, int my) {
    int count;
    int visible_count;
    int btn_width;
    int start_x;

    if (is_start_open()) return 0;
    if (my < SCREEN_HEIGHT - 20) return 0;

    if (mx < 50) {
        if (!kernel_is_start_page_enabled()) return 1;
        klog_write("start click");
        if (!console_launch_tsk("system/start.tsk")) {
            klog_write("start launch failed");
            kpanic("cannot launch system/start.tsk");
        }
        return 1;
    }

    count = win_get_count();
    visible_count = count_taskbar_windows();
    btn_width = get_taskbar_button_width(visible_count);
    start_x = 55;

    if (btn_width <= 0) return 0;

    for (int i = 0; i < count; i++) {
        Window* w = win_get_at_layer(i);

        if (!w || !w->visible || is_start_window(w)) continue;
        if (mx >= start_x && mx < start_x + btn_width) {
            win_bring_to_front(w);
            return 1;
        }
        start_x += btn_width + 2;
    }

    return 0;
}
