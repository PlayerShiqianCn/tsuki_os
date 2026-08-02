#include "console.h"
#include "utils.h"
#include "video.h"
#include "fs.h"
#include "heap.h"
#include "process.h"
#include "window.h"
#include "klog.h"
#include "mp.h"
#include "kernel_core.h"
#include "paging.h"
#include "syscall.h"

static int app_launch_serial = 0;
#define HIDDEN_SUFFIX "._hid_"

static int str_ends_with_local(const char* s, const char* suffix) {
    int ls = strlen(s);
    int lf = strlen(suffix);
    if (ls < lf) return 0;
    return strcmp(s + (ls - lf), suffix) == 0;
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
// 职责：将窗口内部的 Back Buffer 复制到屏幕后缓冲
void app_window_render(Window* w) {
    unsigned int* buf = (unsigned int*)MP_VIDEO_BACK_BUFFER_BASE;
    int start_x = w->x + BORDER_WIDTH;
    int start_y = w->y + TITLE_BAR_HEIGHT;
    int client_w = w->w - BORDER_WIDTH * 2;
    int client_h = w->h - TITLE_BAR_HEIGHT - BORDER_WIDTH;

    if (client_w <= 0 || client_h <= 0 || !w->buffer) return;

    /* Clip to screen */
    int x0 = (start_x < 0) ? 0 : start_x;
    int y0 = (start_y < 0) ? 0 : start_y;
    int x1 = (start_x + client_w > SCREEN_WIDTH) ? SCREEN_WIDTH : (start_x + client_w);
    int y1 = (start_y + client_h > SCREEN_HEIGHT) ? SCREEN_HEIGHT : (start_y + client_h);
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; y++) {
        int src_row = y - start_y;
        unsigned int* src = w->buffer + (src_row + TITLE_BAR_HEIGHT) * w->w + BORDER_WIDTH + (x0 - start_x);
        unsigned int* dst = buf + y * SCREEN_WIDTH + x0;
        int cols = x1 - x0;
        while (cols--) *dst++ = *src++ & 0x00FFFFFFu;
    }
}

int console_launch_tsk(const char* filename) {
    return console_launch_tsk_ex(filename, TSK_LAUNCH_ACTIVATE);
}

int console_launch_tsk_ex(const char* filename, int flags) {
    const char* file_id;
    char canonical_name[FS_FILENAME_LEN];
    TskImageInfo image;
    PagingSpace space;
    Process* existing;
    void* load_destination;
    void* entry_point;
    unsigned int instance_id;

    if (!filename) return 0;
    klog_write_pair("launch ", filename);
    filename = normalize_tsk_name(filename);
    if (!path_canonical_leaf(filename, canonical_name, sizeof(canonical_name))) return 0;
    file_id = canonical_name;

    if (!tsk_probe(filename, &image)) {
        klog_write_pair("tsk_probe fail ", filename);
        return 0;
    }
    if (!(flags & TSK_LAUNCH_NEW_INSTANCE)) {
        existing = process_find_by_image_inode(image.inode_num);
        if (existing && existing->win) {
            win_bring_to_front(existing->win);
            return 1;
        }
    }

    memset(&space, 0, sizeof(space));
    space.app_physical_slot = -1;
    if (!paging_create_process_space(image.load_addr, image.image_size, &space)) {
        klog_write_pair("page space fail ", file_id);
        return 0;
    }
    load_destination = paging_app_alias(space.app_physical_slot);
    if (!load_destination || !tsk_load_to(filename, load_destination, &image)) {
        klog_write_pair("tsk_load fail ", filename);
        paging_destroy_process_space(space.cr3, space.app_physical_slot);
        return 0;
    }
    entry_point = (void*)(image.load_addr + image.entry_offset);

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

    instance_id = (unsigned int)++app_launch_serial;

    const char* window_title = file_id;
    if (strcmp(file_id, "image.tsk") == 0) {
        window_title = "JPEG Viewer";
    } else if (strcmp(file_id, "settings.tsk") == 0) {
        window_title = "Settings";
    }

    Window* app_win = win_create(x, y, w, h, (char*)window_title, C_BLACK);
    if (!app_win) {
        klog_write_pair("win_create fail ", file_id);
        paging_destroy_process_space(space.cr3, space.app_physical_slot);
        return 0;
    }

    app_win->extra_draw = 0;
    if (strcmp(file_id, "start.tsk") == 0) app_win->borderless = 1;
    if (!process_create((void (*)())entry_point, canonical_name, app_win,
                        image.load_addr, image.load_addr + image.image_size,
                        space.cr3, space.app_physical_slot, image.inode_num,
                        instance_id)) {
        klog_write_pair("proc create fail ", file_id);
        win_destroy(app_win);
        paging_destroy_process_space(space.cr3, space.app_physical_slot);
        return 0;
    }
    klog_write_pair("launch ok ", file_id);
    video_request_redraw();
    return 1;
}
