#include "lib.h"

#define LOG_COLS 24
#define LOG_ROWS 8

static unsigned char stack[4096];
static char logs[LOG_ROWS][LOG_COLS + 1];
static int log_row = 0;
static int log_col = 0;
static int is_focused = 0;

void main();

__attribute__((naked)) void _start() {
    __asm__ volatile (
        "movl %0, %%esp \n\t"
        "call main \n\t"
        "call exit \n\t"
        :
        : "r" (stack + sizeof(stack))
        : "memory"
    );
}

static void clear_logs(void) {
    for (int i = 0; i < LOG_ROWS; i++) logs[i][0] = '\0';
    log_row = 0;
    log_col = 0;
}

static void scroll_logs(void) {
    for (int r = 1; r < LOG_ROWS; r++) {
        for (int c = 0; c <= LOG_COLS; c++) {
            logs[r - 1][c] = logs[r][c];
        }
    }
    logs[LOG_ROWS - 1][0] = '\0';
}

static void log_char(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') {
        log_col = 0;
        log_row++;
        if (log_row >= LOG_ROWS) {
            scroll_logs();
            log_row = LOG_ROWS - 1;
        }
        return;
    }
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) ch = '.';
    if (log_col >= LOG_COLS) log_char('\n');
    logs[log_row][log_col++] = ch;
    logs[log_row][log_col] = '\0';
}

static void log_text(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) log_char(s[i]);
}

static void log_line(const char* s) {
    log_text(s);
    log_char('\n');
}

static void launch_with_log(const char* file) {
    log_text("launch ");
    log_text(file);
    log_char('\n');
    if (launch_tsk(file)) log_line("ok");
    else log_line("failed");
}

static void show_file_list(void) {
    char list_buf[256];
    list_buf[0] = '\0';
    if (!list_files(list_buf, sizeof(list_buf))) {
        log_line("ls failed");
        return;
    }
    if (list_buf[0] == '\0') {
        log_line("(no files)");
        return;
    }
    log_text(list_buf);
    int n = 0;
    while (list_buf[n]) n++;
    if (list_buf[n - 1] != '\n') log_char('\n');
}

static void render(void) {
    draw_rect(0, 0, 196, 128, 11);
    draw_rect(0, 0, 196, 14, 1);
    draw_text(4, 3, "wm.tsk", 15);
    draw_text(132, 3, is_focused ? "FOCUS" : "BLUR ", is_focused ? 10 : 12);

    draw_text(4, 16, "t:terminal s:start", 15);
    draw_text(4, 24, "a:app      w:wm", 15);
    draw_text(4, 32, "l:ls c:clear q:exit", 15);

    draw_rect(0, 42, 196, 84, 0);
    for (int i = 0; i < LOG_ROWS; i++) {
        draw_text(0, 44 + i * 10, logs[i], 10);
    }
}

void main() {
    set_sandbox(1);
    win_set_title("wm.tsk");
    clear_logs();
    log_line("wm ready");
    is_focused = win_is_focused();
    render();

    while (1) {
        int ev = win_get_event();
        if (ev & WIN_EVENT_FOCUS_CHANGED) {
            is_focused = win_is_focused();
            render();
        }

        if (!(ev & WIN_EVENT_KEY_READY)) continue;

        int key = get_key();
        if (!key) continue;

        if (key == 't') launch_with_log("terminal.tsk");
        else if (key == 's') launch_with_log("start.tsk");
        else if (key == 'a') launch_with_log("app.tsk");
        else if (key == 'w') launch_with_log("wm.tsk");
        else if (key == 'l') show_file_list();
        else if (key == 'c') clear_logs();
        else if (key == 'q') {
            exit();
            return;
        } else {
            log_line("unknown key");
        }

        render();
    }
}
