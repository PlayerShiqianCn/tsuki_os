#include "syscall.h"
#include "console.h"
#include "fs.h"
#include "video.h"
#include "heap.h"
#include "process.h"
#include "window.h"
#include "ps2.h"
#include "font8x8_basic.h"

static SandboxLevel get_current_sandbox_level(void) {
    if (!current_process) return SANDBOX_NONE;
    return (SandboxLevel)current_process->sandbox_level;
}

static int is_syscall_allowed(SandboxLevel level, unsigned int sysno) {
    if (level == SANDBOX_NONE) return 1;

    if (level == SANDBOX_BASIC) {
        // BASIC: 允许常规 GUI/输入/窗口接口
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
    int can_present = (win_get_focused() == win);
    int offset_x = BORDER_WIDTH;
    int offset_y = TITLE_BAR_HEIGHT;
    int client_w = win->w - (BORDER_WIDTH * 2);
    int client_h = win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH;

    // 裁剪
    if (x >= client_w || y >= client_h) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > client_w) w = client_w - x;
    if (y + h > client_h) h = client_h - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            win_put_pixel(win, x + j + offset_x, y + i + offset_y, (unsigned char)color);
        }
    }

    if (can_present) {
        draw_rect(win->x + x + offset_x, win->y + y + offset_y, w, h, (unsigned char)color);
        video_swap_buffer();
    }
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
    int can_present = (win_get_focused() == win);
    int offset_x = BORDER_WIDTH;
    int offset_y = TITLE_BAR_HEIGHT;
    int client_w = win->w - (BORDER_WIDTH * 2);
    int client_h = win->h - TITLE_BAR_HEIGHT - BORDER_WIDTH;
    int cursor_x = x;
    int cursor_y = y;

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

                win_put_pixel(win, px + offset_x, py + offset_y, (unsigned char)color);
                if (can_present) {
                    draw_pixel(win->x + px + offset_x, win->y + py + offset_y, (unsigned char)color);
                }
            }
        }

        cursor_x += 8;
    }

    if (can_present) {
        video_swap_buffer();
    }
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
                fs_get_file_list((char*)regs->ebx, regs->ecx);
                regs->eax = 1;
            } else {
                regs->eax = 0;
            }
            break;

        case SYS_LAUNCH_TSK:
            regs->eax = console_launch_tsk((const char*)regs->ebx);
            break;

        default:
            regs->eax = 0;
            break;
    }
}

void syscall_init() {}
