#include "lib.h"

#define TERM_COLS 24
#define TERM_OUTPUT_ROWS 12
#define INPUT_MAX 64
#define FILE_BUF_SIZE 2048

static unsigned char stack[4096];

static char term_lines[TERM_OUTPUT_ROWS][TERM_COLS + 1];
static int out_row = 0;
static int out_col = 0;
static char input_buf[INPUT_MAX];
static int input_len = 0;
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

static int s_len(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int s_cmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        i++;
    }
    return (int)((unsigned char)a[i] - (unsigned char)b[i]);
}

static int s_ncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)(ac - bc);
        if (ac == 0) return 0;
    }
    return 0;
}

static int s_ends_with(const char* s, const char* suffix) {
    int ls = s_len(s);
    int lf = s_len(suffix);
    if (ls < lf) return 0;
    return s_cmp(s + (ls - lf), suffix) == 0;
}

static void s_copy(char* dst, const char* src, int max) {
    if (max <= 0) return;
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void clear_output(void) {
    for (int r = 0; r < TERM_OUTPUT_ROWS; r++) {
        term_lines[r][0] = '\0';
    }
    out_row = 0;
    out_col = 0;
}

static void scroll_output(void) {
    for (int r = 1; r < TERM_OUTPUT_ROWS; r++) {
        for (int c = 0; c <= TERM_COLS; c++) {
            term_lines[r - 1][c] = term_lines[r][c];
        }
    }
    term_lines[TERM_OUTPUT_ROWS - 1][0] = '\0';
}

static void push_char(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') {
        out_col = 0;
        out_row++;
        if (out_row >= TERM_OUTPUT_ROWS) {
            scroll_output();
            out_row = TERM_OUTPUT_ROWS - 1;
        }
        return;
    }
    if (ch == '\t') {
        for (int i = 0; i < 4; i++) push_char(' ');
        return;
    }
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) {
        ch = '.';
    }
    if (out_col >= TERM_COLS) {
        push_char('\n');
    }
    term_lines[out_row][out_col++] = ch;
    term_lines[out_row][out_col] = '\0';
}

static void write_text(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) push_char(s[i]);
}

static void write_line(const char* s) {
    write_text(s);
    push_char('\n');
}

static void render(void) {
    draw_rect(0, 0, 196, 128, 0);
    draw_rect(0, 0, 196, 14, 1);
    draw_text(4, 3, "terminal.tsk", 15);
    draw_text(128, 3, is_focused ? "FOCUS" : "BLUR ", is_focused ? 10 : 12);
    draw_rect(0, 14, 196, 1, 8);

    for (int r = 0; r < TERM_OUTPUT_ROWS; r++) {
        draw_text(0, 16 + r * 8, term_lines[r], 10);
    }

    char prompt[TERM_COLS + 1];
    int max_visible_input = TERM_COLS - 2;
    int start = (input_len > max_visible_input) ? (input_len - max_visible_input) : 0;
    int p = 0;
    prompt[p++] = '>';
    prompt[p++] = ' ';
    for (int i = start; i < input_len && p < TERM_COLS; i++) {
        prompt[p++] = input_buf[i];
    }
    prompt[p] = '\0';
    draw_rect(0, 16 + TERM_OUTPUT_ROWS * 8, 196, 8, 0);
    draw_text(0, 16 + TERM_OUTPUT_ROWS * 8, prompt, 15);
}

static char* ltrim(char* s) {
    while (*s == ' ') s++;
    return s;
}

static void rtrim(char* s) {
    int n = s_len(s);
    while (n > 0 && s[n - 1] == ' ') {
        s[n - 1] = '\0';
        n--;
    }
}

static void launch_and_report(const char* filename) {
    write_text("Launching ");
    write_text(filename);
    push_char('\n');
    if (launch_tsk(filename)) {
        write_line("Process started/focused.");
    } else {
        write_line("Launch failed.");
    }
}

static void run_command(const char* raw_cmd) {
    char cmd[INPUT_MAX];
    s_copy(cmd, raw_cmd, INPUT_MAX);

    char* c = ltrim(cmd);
    rtrim(c);
    if (c[0] == '\0') return;

    if (s_cmp(c, "help") == 0) {
        write_line("Commands:");
        write_line("help  cls  ls  cat <file>");
        write_line("run <name.tsk>  <name.tsk>  exit");
        return;
    }

    if (s_cmp(c, "cls") == 0) {
        clear_output();
        return;
    }

    if (s_cmp(c, "ls") == 0) {
        char list_buf[512];
        list_buf[0] = '\0';
        if (!list_files(list_buf, sizeof(list_buf))) {
            write_line("ls failed.");
            return;
        }
        if (list_buf[0] == '\0') {
            write_line("(no files)");
            return;
        }
        write_text(list_buf);
        if (list_buf[s_len(list_buf) - 1] != '\n') push_char('\n');
        return;
    }

    if (s_ncmp(c, "cat ", 4) == 0) {
        char* name = ltrim(c + 4);
        if (name[0] == '\0') {
            write_line("Usage: cat <filename>");
            return;
        }
        char file_buf[FILE_BUF_SIZE];
        int n = read_file(name, file_buf);
        if (n <= 0) {
            write_line("Read failed.");
            return;
        }
        if (n >= FILE_BUF_SIZE) n = FILE_BUF_SIZE - 1;
        file_buf[n] = '\0';
        write_text(file_buf);
        if (file_buf[n - 1] != '\n') push_char('\n');
        return;
    }

    if (s_ncmp(c, "run ", 4) == 0) {
        char* name = ltrim(c + 4);
        if (name[0] == '\0') {
            write_line("Usage: run <name.tsk>");
            return;
        }
        launch_and_report(name);
        return;
    }

    if (s_cmp(c, "exit") == 0) {
        write_line("Bye.");
        render();
        exit();
        return;
    }

    if (s_ends_with(c, ".tsk")) {
        launch_and_report(c);
        return;
    }

    write_text("Unknown command: ");
    write_line(c);
}

void main() {
    set_sandbox(1);
    win_set_title("terminal.tsk");

    clear_output();
    input_buf[0] = '\0';
    input_len = 0;
    write_line("TSK terminal ready.");
    write_line("Type help for commands.");

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

        if (key == '\n') {
            write_text("> ");
            write_text(input_buf);
            push_char('\n');
            run_command(input_buf);
            input_len = 0;
            input_buf[0] = '\0';
            render();
            continue;
        }

        if (key == '\b') {
            if (input_len > 0) {
                input_len--;
                input_buf[input_len] = '\0';
            }
            render();
            continue;
        }

        if (key >= 32 && key <= 126 && input_len < INPUT_MAX - 1) {
            input_buf[input_len++] = (char)key;
            input_buf[input_len] = '\0';
            render();
        }
    }
}
