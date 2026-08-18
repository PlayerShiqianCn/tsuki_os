__asm__(".code32");

#include "desktop.h"

#include "console.h"
#include "kernel_config.h"
#include "klog.h"
#include "utils.h"
#include "video.h"
#include "window.h"
#include "theme.h"

static int is_start_window(Window* w) {
    return (w && w->title && strcmp(w->title, "start.tsk") == 0);
}

static int is_start_open(void) {
    Window* snapshot[MAX_LAYERS];
    int count = win_snapshot_layers(snapshot, MAX_LAYERS);
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (is_start_window(snapshot[i])) { found = 1; break; }
    }
    win_release_snapshot(snapshot, count);
    return found;
}

static int get_taskbar_button_width(int button_count) {
    int available = SCREEN_WIDTH - 76;
    int gap = 6;
    int width;

    if (button_count <= 0) return 0;

    width = (available - (button_count - 1) * gap) / button_count;
    if (width < 72) width = 72;
    if (width > 150) width = 150;
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
            draw_rect_rgb(0, 0, SCREEN_WIDTH, 122, 0x20263Au);
            draw_rect_rgb(0, 122, SCREEN_WIDTH, SCREEN_HEIGHT - 122, 0x181825u);
            draw_round_rect_rgb(18, 18, 38, 14, 6, 0x2D354Fu);
            break;
        case 2:
            draw_rect_rgb(0, 0, SCREEN_WIDTH, 126, 0x20312Eu);
            draw_rect_rgb(0, 126, SCREEN_WIDTH, SCREEN_HEIGHT - 126, 0x182521u);
            draw_rect_rgb(0, 150, SCREEN_WIDTH, 6, 0x33423Bu);
            break;
        default:
            draw_rect_rgb(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TSUKI_RGB_BG);
            break;
    }
}

void desktop_draw_taskbar(void) {
    Window* snapshot[MAX_LAYERS];
    int count = win_snapshot_layers(snapshot, MAX_LAYERS);
    int visible_count = 0;
    int btn_width;
    int start_x = 72;

    for (int i = 0; i < count; i++) {
        if (!is_start_window(snapshot[i])) visible_count++;
    }
    if (visible_count != count) {
        win_release_snapshot(snapshot, count);
        return;
    }

    draw_rect_rgb(0, SCREEN_HEIGHT - TSUKI_TASKBAR_HEIGHT, SCREEN_WIDTH, TSUKI_TASKBAR_HEIGHT, TSUKI_RGB_SURFACE);
    draw_rect_rgb(0, SCREEN_HEIGHT - TSUKI_TASKBAR_HEIGHT, SCREEN_WIDTH, 1, TSUKI_RGB_BORDER);
    draw_round_rect_rgb(6, SCREEN_HEIGHT - 25, 56, 20, TSUKI_RADIUS,
                        kernel_is_start_page_enabled() ? TSUKI_RGB_CONTROL : TSUKI_RGB_RED);
    draw_string_rgb(18, SCREEN_HEIGHT - 21,
                    kernel_is_start_page_enabled() ? "TSK" : "OFF", TSUKI_RGB_TEXT);
    btn_width = get_taskbar_button_width(visible_count);

    for (int i = 0; i < count; i++) {
        Window* w = snapshot[i];
        char label[16];
        if (!w || !w->visible) continue;
        draw_round_rect_rgb(start_x, SCREEN_HEIGHT - 25, btn_width, 20, TSUKI_RADIUS,
                            i == count - 1 ? TSUKI_RGB_CONTROL_HOVER : TSUKI_RGB_CONTROL);
        format_taskbar_title(w->title, label, sizeof(label), btn_width);
        draw_string_rgb(start_x + 8, SCREEN_HEIGHT - 21, label,
                        i == count - 1 ? TSUKI_RGB_TEXT : TSUKI_RGB_MUTED);
        if (i == count - 1) draw_round_rect_rgb(start_x + 8, SCREEN_HEIGHT - 4, btn_width - 16, 2, 1, TSUKI_RGB_ACCENT);
        start_x += btn_width + 6;
    }
    win_release_snapshot(snapshot, count);
}

int desktop_handle_taskbar_click(int mx, int my) {
    Window* snapshot[MAX_LAYERS];
    int count;
    int visible_count = 0;
    int btn_width;
    int start_x = 72;

    if (is_start_open()) return 0;
    if (my < SCREEN_HEIGHT - TSUKI_TASKBAR_HEIGHT) return 0;
    if (mx < 66) {
        if (!kernel_is_start_page_enabled()) return 1;
        klog_write("start click");
        if (!console_launch_tsk("system/start.tsk")) klog_write("start launch failed");
        return 1;
    }

    count = win_snapshot_layers(snapshot, MAX_LAYERS);
    for (int i = 0; i < count; i++) if (!is_start_window(snapshot[i])) visible_count++;
    btn_width = get_taskbar_button_width(visible_count);
    if (btn_width <= 0) { win_release_snapshot(snapshot, count); return 0; }

    for (int i = 0; i < count; i++) {
        Window* w = snapshot[i];
        if (!w || !w->visible || is_start_window(w)) continue;
        if (mx >= start_x && mx < start_x + btn_width) {
            win_bring_to_front(w);
            win_release_snapshot(snapshot, count);
            return 1;
        }
        start_x += btn_width + 6;
    }
    win_release_snapshot(snapshot, count);
    return 0;
}
