#include "syscall.h"
#include "console.h"
#include "fs.h"
#include "video.h"
#include "heap.h"
#include "process.h"
#include "window.h"
#include "ps2.h"
#include "font8x8_basic.h"
#include "net.h"

void kernel_set_wallpaper_style(int style);
void kernel_set_start_page_enabled(int enabled);
void kernel_reload_system_config(void);

// 动态开始菜单磁贴内核态存储
#define MAX_DYNAMIC_TILES 16
typedef struct {
    char title[16];
    char file[16];
    int color;
    int x; // 用于渲染排版预留
    int y; // 用于渲染排版预留
} StartTile;

static StartTile dynamic_tiles[MAX_DYNAMIC_TILES];
static int dynamic_tile_count = 0;
static int start_registry_loaded = 0;

static int tile_string_eq(const char* a, const char* b) {
    int i;
    if (!a || !b) return 0;
    for (i = 0; i < 16; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static void tile_copy_field(char* dst, const char* src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; i < 15 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int tile_parse_int(const char* src) {
    int sign = 1;
    int value = 0;
    int i = 0;
    if (!src) return 0;
    if (src[0] == '-') {
        sign = -1;
        i = 1;
    }
    for (; src[i] >= '0' && src[i] <= '9'; i++) {
        value = value * 10 + (src[i] - '0');
    }
    return value * sign;
}

static int find_dynamic_tile_by_file(const char* file) {
    int i;
    if (!file) return -1;
    for (i = 0; i < dynamic_tile_count; i++) {
        if (tile_string_eq(dynamic_tiles[i].file, file)) return i;
    }
    return -1;
}

static int upsert_dynamic_tile(const char* title, const char* file, int color) {
    int idx;
    if (!title || !file || !file[0]) return 0;

    idx = find_dynamic_tile_by_file(file);
    if (idx < 0) {
        if (dynamic_tile_count >= MAX_DYNAMIC_TILES) return 0;
        idx = dynamic_tile_count++;
    }

    tile_copy_field(dynamic_tiles[idx].title, title);
    tile_copy_field(dynamic_tiles[idx].file, file);
    dynamic_tiles[idx].color = color;
    dynamic_tiles[idx].x = 0;
    dynamic_tiles[idx].y = 0;
    return 1;
}

static void load_start_registry(void) {
    SystemFile file;
    char* buf;
    int read;
    int pos;

    if (start_registry_loaded) return;
    start_registry_loaded = 1;
    dynamic_tile_count = 0;

    if (!sys_file_open("system/start.rtsk", &file)) return;
    if (file.size == 0) return;

    buf = (char*)malloc(file.size + 1);
    if (!buf) return;

    read = fs_read_file("system/start.rtsk", buf);
    if (read <= 0) {
        free(buf);
        return;
    }
    if ((unsigned int)read > file.size) read = (int)file.size;
    buf[read] = '\0';

    pos = 0;
    while (pos < read && dynamic_tile_count < MAX_DYNAMIC_TILES) {
        char title[16];
        char path[16];
        char color_buf[16];
        int ti = 0;
        int fi = 0;
        int ci = 0;
        int valid = 1;

        while (pos < read && (buf[pos] == '\r' || buf[pos] == '\n')) pos++;
        if (pos >= read) break;
        if (buf[pos] == '#') {
            while (pos < read && buf[pos] != '\n') pos++;
            continue;
        }

        while (pos < read && buf[pos] != '|' && buf[pos] != '\n' && ti < 15) {
            title[ti++] = buf[pos++];
        }
        title[ti] = '\0';
        if (pos >= read || buf[pos] != '|') valid = 0;
        if (!valid) {
            while (pos < read && buf[pos] != '\n') pos++;
            continue;
        }

        pos++;
        while (pos < read && buf[pos] != '|' && buf[pos] != '\n' && fi < 15) {
            path[fi++] = buf[pos++];
        }
        path[fi] = '\0';
        if (pos >= read || buf[pos] != '|') valid = 0;
        if (!valid) {
            while (pos < read && buf[pos] != '\n') pos++;
            continue;
        }

        pos++;
        while (pos < read && buf[pos] != '\n' && ci < 15) {
            color_buf[ci++] = buf[pos++];
        }
        color_buf[ci] = '\0';

        while (pos < read && buf[pos] != '\n') pos++;
        if (pos < read && buf[pos] == '\n') pos++;

        if (!title[0] || !path[0]) continue;
        upsert_dynamic_tile(title, path, tile_parse_int(color_buf));
    }

    free(buf);
}

static int append_registry_text(char* buf, int max_len, int* pos, const char* text) {
    int i = 0;
    if (!buf || !pos || !text) return 0;
    while (text[i]) {
        if (*pos >= max_len) return 0;
        buf[*pos] = text[i];
        (*pos)++;
        i++;
    }
    return 1;
}

static int append_registry_int(char* buf, int max_len, int* pos, int value) {
    char tmp[12];
    int len = 0;
    int n = value;

    if (n == 0) {
        if (*pos >= max_len) return 0;
        buf[(*pos)++] = '0';
        return 1;
    }

    if (n < 0) {
        if (*pos >= max_len) return 0;
        buf[(*pos)++] = '-';
        n = -n;
    }

    while (n > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (n % 10));
        n /= 10;
    }

    while (len > 0) {
        if (*pos >= max_len) return 0;
        buf[*pos] = tmp[--len];
        (*pos)++;
    }
    return 1;
}

static int save_start_registry(void) {
    char* buf = (char*)malloc(1024);
    int pos = 0;
    int ok = 1;

    if (!buf) return 0;

    ok = append_registry_text(buf, 1024, &pos, "# title|file|color\n");
    for (int i = 0; ok && i < dynamic_tile_count; i++) {
        ok = append_registry_text(buf, 1024, &pos, dynamic_tiles[i].title);
        if (ok) ok = append_registry_text(buf, 1024, &pos, "|");
        if (ok) ok = append_registry_text(buf, 1024, &pos, dynamic_tiles[i].file);
        if (ok) ok = append_registry_text(buf, 1024, &pos, "|");
        if (ok) ok = append_registry_int(buf, 1024, &pos, dynamic_tiles[i].color);
        if (ok) ok = append_registry_text(buf, 1024, &pos, "\n");
    }

    if (ok) {
        ok = (fs_write_file("system/start.rtsk", buf, (unsigned int)pos) == pos);
    }

    free(buf);
    return ok;
}

static SandboxLevel get_current_sandbox_level(void) {
    if (!current_process) return SANDBOX_NONE;
    return (SandboxLevel)current_process->sandbox_level;
}

static int is_syscall_allowed(SandboxLevel level, unsigned int sysno) {
    if (level == SANDBOX_NONE) return 1;

    if (level == SANDBOX_BASIC) {
        // BASIC: 允许常规 GUI/输入/窗口接口
        if (sysno == SYS_WRITE_FILE) return 0;
        return 1;
    }

    // STRICT: 只保留最小交互能力
    if (sysno == SYS_EXIT ||
        sysno == SYS_DRAW_RECT ||
        sysno == SYS_DRAW_TEXT ||
        sysno == SYS_GET_KEY ||
        sysno == SYS_WIN_IS_FOCUSED ||
        sysno == SYS_WIN_GET_EVENT ||
        sysno == SYS_SET_SANDBOX) {
        return 1;
    }
    return 0;
}

static int is_current_process_focused(void) {
    if (!current_process || !current_process->win) return 0;
    return win_get_focused() == current_process->win;
}

void sys_draw_rect_sandboxed(int x, int y, int w, int h, int color) {
    // 无窗口模式：直接画到屏幕
    if (!current_process || !current_process->win) {
        draw_rect(x, y, w, h, (unsigned char)color);
        video_swap_buffer();
        return;
    }

    Window* win = current_process->win;
    int offset_x = win->borderless ? 0 : BORDER_WIDTH;
    int offset_y = win->borderless ? 0 : TITLE_BAR_HEIGHT;
    int client_w = win->borderless ? win->w : (win->w - BORDER_WIDTH * 2);
    int client_h = win->borderless ? win->h : (win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH);

    // 裁剪
    if (x >= client_w || y >= client_h) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > client_w) w = client_w - x;
    if (y + h > client_h) h = client_h - y;
    if (w <= 0 || h <= 0) return;

    // 只写入窗口缓冲区，由内核主循环统一刷新屏幕
    {
        unsigned int rgb = video_color_to_rgb((unsigned char)color);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                win_put_pixel(win, x + j + offset_x, y + i + offset_y, rgb);
            }
        }
    }
    video_request_redraw();
}

static int get_font_index(char c) {
    unsigned char uc = (unsigned char)c;
    if (uc < 128) return (int)uc;
    return 0;
}

static void sys_draw_text_sandboxed(int x, int y, const char* str, int color) {
    if (!str) return;

    if (!current_process || !current_process->win) {
        draw_string(x, y, (char*)str, (unsigned char)color);
        video_swap_buffer();
        return;
    }

    Window* win = current_process->win;
    int offset_x = win->borderless ? 0 : BORDER_WIDTH;
    int offset_y = win->borderless ? 0 : TITLE_BAR_HEIGHT;
    int client_w = win->borderless ? win->w : (win->w - BORDER_WIDTH * 2);
    int client_h = win->borderless ? win->h : (win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH);
    int cursor_x = x;
    int cursor_y = y;
    unsigned int rgb = video_color_to_rgb((unsigned char)color);

    // 只写入窗口缓冲区，由内核主循环统一刷新屏幕
    for (int idx = 0; str[idx] != '\0'; idx++) {
        char c = str[idx];
        if (c == '\n') {
            cursor_x = x;
            cursor_y += 8;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') {
            cursor_x += 16;
            continue;
        }

        int font_idx = get_font_index(c);
        for (int row = 0; row < 8; row++) {
            int py = cursor_y + row;
            if (py < 0 || py >= client_h) continue;

            unsigned char bits = (unsigned char)font8x8_basic[font_idx][row];
            for (int col = 0; col < 8; col++) {
                if (((bits >> col) & 1) == 0) continue;

                int px = cursor_x + col;
                if (px < 0 || px >= client_w) continue;

                win_put_pixel(win, px + offset_x, py + offset_y, rgb);
            }
        }

        cursor_x += 8;
    }
    video_request_redraw();
}

static void sys_draw_rect_rgb_sandboxed(int x, int y, int w, int h, unsigned int rgb) {
    if (!current_process || !current_process->win) {
        draw_rect_rgb(x, y, w, h, rgb);
        video_swap_buffer();
        return;
    }

    Window* win = current_process->win;
    int offset_x = win->borderless ? 0 : BORDER_WIDTH;
    int offset_y = win->borderless ? 0 : TITLE_BAR_HEIGHT;
    int client_w = win->borderless ? win->w : (win->w - BORDER_WIDTH * 2);
    int client_h = win->borderless ? win->h : (win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH);

    if (x >= client_w || y >= client_h) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > client_w) w = client_w - x;
    if (y + h > client_h) h = client_h - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            win_put_pixel(win, x + j + offset_x, y + i + offset_y, rgb);
        }
    }
    video_request_redraw();
}

static int sys_win_create_sandboxed(int x, int y, int w, int h, const char* title) {
    if (!current_process || current_process->pid == 0) return 0;

    if (!title) title = "tsk";
    if (w < 64) w = 64;
    if (h < 48) h = 48;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w > SCREEN_WIDTH) w = SCREEN_WIDTH;
    if (h > SCREEN_HEIGHT) h = SCREEN_HEIGHT;
    if (x + w > SCREEN_WIDTH) x = SCREEN_WIDTH - w;
    if (y + h > SCREEN_HEIGHT) y = SCREEN_HEIGHT - h;

    Window* new_win = win_create(x, y, w, h, (char*)title, C_BLACK);
    if (!new_win) return 0;

    if (current_process->win) {
        win_destroy(current_process->win);
    }

    current_process->win = new_win;
    current_process->focus_state_cache = -1;
    win_bring_to_front(new_win);
    return 1;
}

static int sys_win_set_title_sandboxed(const char* title) {
    if (!current_process || !current_process->win || !title) return 0;
    return win_set_title(current_process->win, title);
}

void syscall_handler(Registers* regs) {
    SandboxLevel level = get_current_sandbox_level();
    if (!is_syscall_allowed(level, regs->eax)) {
        regs->eax = 0;
        return;
    }

    switch (regs->eax) {
        case SYS_EXIT:
            process_exit();
            break;

        case SYS_PRINT:
            // 内核终端已移除，SYS_PRINT 不再有效
            // tsk 应用通过 draw_text 系统调用进行窗口内输出
            break;

        case SYS_READ_FILE:
            regs->eax = fs_read_file((char*)regs->ebx, (void*)regs->ecx);
            break;

        case SYS_DRAW_RECT:
            // ebx=x, ecx=y, edx=w, esi=h, edi=color
            sys_draw_rect_sandboxed(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
            break;

        case SYS_DRAW_RECT_RGB:
            // ebx=x, ecx=y, edx=w, esi=h, edi=0xRRGGBB
            sys_draw_rect_rgb_sandboxed(regs->ebx, regs->ecx, regs->edx, regs->esi, (unsigned int)regs->edi);
            break;

        case SYS_SLEEP:
            // TODO: 实现 sleep
            break;

        case SYS_SET_SANDBOX:
            if (current_process) {
                current_process->sandbox_level = regs->ebx;
            }
            break;

        case SYS_GET_KEY:
            if (is_current_process_focused()) {
                regs->eax = (unsigned char)ps2_getchar();
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_DRAW_TEXT:
            // ebx=x, ecx=y, edx=str, esi=color
            sys_draw_text_sandboxed(regs->ebx, regs->ecx, (const char*)regs->edx, regs->esi);
            break;

        case SYS_WIN_CREATE:
            // ebx=x, ecx=y, edx=w, esi=h, edi=title
            regs->eax = sys_win_create_sandboxed(
                regs->ebx, regs->ecx, regs->edx, regs->esi, (const char*)regs->edi
            );
            break;

        case SYS_WIN_SET_TITLE:
            // ebx=title
            regs->eax = sys_win_set_title_sandboxed((const char*)regs->ebx);
            break;

        case SYS_WIN_IS_FOCUSED:
            regs->eax = is_current_process_focused();
            break;

        case SYS_WIN_GET_EVENT: {
            int events = 0;
            int focused = is_current_process_focused();

            if (current_process) {
                if (current_process->focus_state_cache < 0) {
                    current_process->focus_state_cache = focused;
                }

                if (focused != current_process->focus_state_cache) {
                    events |= WIN_EVENT_FOCUS_CHANGED;
                    current_process->focus_state_cache = focused;
                }
            }

            if (focused && ps2_has_key()) {
                events |= WIN_EVENT_KEY_READY;
            }

            regs->eax = events;
            break;
        }

        case SYS_FS_LIST:
            if (regs->ebx && regs->ecx > 0) {
                regs->eax = fs_get_file_list((char*)regs->ebx, regs->ecx, (const char*)regs->edx);
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_LAUNCH_TSK:
            regs->eax = console_launch_tsk((const char*)regs->ebx);
            break;

        case SYS_GET_MOUSE_EVENT:
            if (current_process && current_process->has_mouse_event) {
                regs->eax = 1;
                regs->ebx = current_process->mouse_click_x;
                regs->ecx = current_process->mouse_click_y;
                current_process->has_mouse_event = 0;
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_ADD_START_TILE:
            load_start_registry();
            regs->eax = upsert_dynamic_tile((const char*)regs->ebx, (const char*)regs->ecx, regs->edx);
            if (regs->eax) regs->eax = save_start_registry();
            break;

        case SYS_GET_START_TILES:
            load_start_registry();
            if (regs->ebx) {
                StartTile* user_buf = (StartTile*)regs->ebx;
                int max_count = regs->ecx;
                int copy_cnt = dynamic_tile_count < max_count ? dynamic_tile_count : max_count;
                
                for (int i = 0; i < copy_cnt; i++) {
                    user_buf[i] = dynamic_tiles[i];
                }
                regs->eax = copy_cnt;
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_REMOVE_START_TILE:
            load_start_registry();
            if (regs->ebx) {
                const char* target_file = (const char*)regs->ebx;
                int found = find_dynamic_tile_by_file(target_file);
                
                if (found >= 0) {
                    // 移除并整体前移
                    for (int i = found; i < dynamic_tile_count - 1; i++) {
                        dynamic_tiles[i] = dynamic_tiles[i + 1];
                    }
                    dynamic_tile_count--;
                    regs->eax = save_start_registry();
                } else {
                    regs->eax = 0;
                }
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_WRITE_FILE:
            regs->eax = fs_write_file((const char*)regs->ebx, (const void*)regs->ecx, regs->edx);
            if (regs->eax > 0 && regs->ebx &&
                strcmp((const char*)regs->ebx, "system/config.rtsk") == 0) {
                kernel_reload_system_config();
            }
            break;

        case SYS_NET_INFO:
            if (regs->ebx) {
                *(NetDriverInfo*)regs->ebx = *net_get_info();
                regs->eax = 1;
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_NET_PING:
            regs->eax = net_ping_ipv4((unsigned char)regs->ebx, (unsigned char)regs->ecx,
                                      (unsigned char)regs->edx, (unsigned char)regs->esi);
            break;

        case SYS_NET_DNS_QUERY:
            regs->eax = net_dns_query_a((const char*)regs->ebx, (unsigned char*)regs->ecx);
            break;

        case SYS_NET_HTTP_GET:
            regs->eax = net_http_get((const char*)regs->ebx, (const char*)regs->ecx,
                                     (char*)regs->edx, regs->esi, (int*)regs->edi);
            break;

        case SYS_NET_SET_LOCAL_IP:
            net_set_local_ip((unsigned char)regs->ebx, (unsigned char)regs->ecx,
                             (unsigned char)regs->edx, (unsigned char)regs->esi);
            regs->eax = 1;
            break;

        case SYS_NET_SET_GATEWAY:
            net_set_gateway((unsigned char)regs->ebx, (unsigned char)regs->ecx,
                            (unsigned char)regs->edx, (unsigned char)regs->esi);
            regs->eax = 1;
            break;

        case SYS_NET_SET_DNS:
            net_set_dns_server((unsigned char)regs->ebx, (unsigned char)regs->ecx,
                               (unsigned char)regs->edx, (unsigned char)regs->esi);
            regs->eax = 1;
            break;

        case SYS_SET_WALLPAPER_STYLE:
            kernel_set_wallpaper_style(regs->ebx);
            regs->eax = 1;
            break;

        case SYS_SET_START_PAGE_ENABLED:
            kernel_set_start_page_enabled(regs->ebx);
            regs->eax = 1;
            break;

        default:
            regs->eax = 0;
            break;
    }
}

void syscall_init() {}
