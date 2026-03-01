#include "console.h"
#include "utils.h"
#include "video.h"
#include "fs.h"
#include "heap.h"
#include "process.h"
#include "window.h"
#include "klog.h"

static int app_launch_serial = 0;
#define HIDDEN_SUFFIX "._hid_"

static int str_ends_with_local(const char* s, const char* suffix) {
    int ls = strlen(s);
    int lf = strlen(suffix);
    if (ls < lf) return 0;
    return strcmp(s + (ls - lf), suffix) == 0;
}

static const char* path_leaf_local(const char* path) {
    const char* leaf = path;
    if (!path) return "";
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') leaf = path + i + 1;
    }
    return leaf;
}

static const char* normalize_tsk_name(const char* filename) {
    static char norm[FS_FILENAME_LEN];
    if (!filename) return filename;

    int i = 0;
    while (i < FS_FILENAME_LEN - 1 && filename[i]) {
        norm[i] = filename[i];
        i++;
    }
    norm[i] = '\0';

    if (str_ends_with_local(norm, HIDDEN_SUFFIX)) {
        norm[strlen(norm) - strlen(HIDDEN_SUFFIX)] = '\0';
    }

    return norm;
}

// 应用程序窗口的回调函数
// 职责：将窗口内部的 Back Buffer 复制到屏幕显存
void app_window_render(Window* w) {
    // 1. 计算客户区在屏幕上的起始坐标
    int start_x = w->x + BORDER_WIDTH;
    int start_y = w->y + TITLE_BAR_HEIGHT;
    
    // 2. 客户区大小
    int client_w = w->w - BORDER_WIDTH * 2;
    int client_h = w->h - TITLE_BAR_HEIGHT - BORDER_WIDTH;

    // 3. 遍历客户区，将 Window 缓冲区的数据画到屏幕上
    for (int y = 0; y < client_h; y++) {
        for (int x = 0; x < client_w; x++) {
            unsigned int color = win_get_pixel(w, x + BORDER_WIDTH, y + TITLE_BAR_HEIGHT);
            put_pixel_rgb(start_x + x, start_y + y, color);
        }
    }
}

int console_launch_tsk(const char* filename) {
    const char* file_id;
    unsigned int load_base = 0;
    unsigned int image_size = 0;
    if (!filename) return 0;
    klog_write_pair("launch ", filename);
    filename = normalize_tsk_name(filename);
    file_id = path_leaf_local(filename);

    Process* existing = process_find_by_name(filename);
    if (existing && existing->win) {
        win_bring_to_front(existing->win);
        return 1;
    }

    void* entry_point = 0;
    if (!tsk_load(filename, &entry_point, &load_base, &image_size)) {
        klog_write_pair("tsk_load fail ", filename);
        return 0;
    }

    int w = 220;
    int h = 160;
    int x = 52 + (app_launch_serial * 18) % 96;
    int y = 30 + (app_launch_serial * 14) % 70;

    if (strcmp(file_id, "app.tsk") == 0) {
        w = 200;
        h = 150;
    } else if (strcmp(file_id, "terminal.tsk") == 0) {
        w = 240;
        h = 170;
    } else if (strcmp(file_id, "image.tsk") == 0) {
        w = 300;
        h = 180;
        x = 8;
        y = 6;
    } else if (strcmp(file_id, "settings.tsk") == 0) {
        w = 300;
        h = 180;
        x = 12;
        y = 10;
    } else if (strcmp(file_id, "start.tsk") == 0) {
        w = SCREEN_WIDTH;
        h = SCREEN_HEIGHT;
        x = 0;
        y = 0;
    }

    app_launch_serial++;

    const char* window_title = file_id;
    if (strcmp(file_id, "image.tsk") == 0) {
        window_title = "JPEG Viewer";
    } else if (strcmp(file_id, "settings.tsk") == 0) {
        window_title = "Settings";
    }

    Window* app_win = win_create(x, y, w, h, (char*)window_title, C_BLACK);
    if (!app_win) {
        klog_write_pair("win_create fail ", file_id);
        return 0;
    }

    app_win->extra_draw = 0;
    if (strcmp(file_id, "start.tsk") == 0) {
        app_win->borderless = 1;
    }
    if (!process_create((void (*)())entry_point, filename, app_win,
                        load_base, load_base + image_size)) {
        klog_write_pair("proc create fail ", file_id);
        win_destroy(app_win);
        return 0;
    }
    klog_write_pair("launch ok ", file_id);
    video_request_redraw();
    return 1;
}
