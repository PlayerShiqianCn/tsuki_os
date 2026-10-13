// terminal_tsk.c — 终端外壳：入口、事件循环、布局渲染、输出缓冲。
// 命令的实现与分发在 terminal_cmd.c，共享声明在 terminal_internal.h。

#include "terminal_internal.h"

// 动态布局全局量 —— 由 recompute_layout() 重算
static int term_client_w = TERM_DEFAULT_W;
static int term_client_h = TERM_DEFAULT_H;
static int term_input_w = TERM_DEFAULT_W - 20;
static int term_input_y = TERM_DEFAULT_H - TERM_INPUT_H - 10;
static int term_cols = (TERM_DEFAULT_W - TERM_SCROLL_W - TERM_PADDING * 2) / 8;
static int term_output_rows = ((TERM_DEFAULT_H - TERM_INPUT_H - 10) - TERM_PADDING) / TERM_ROW_HEIGHT;

static char history[HISTORY_MAX][HISTORY_MAX_COLS + 1];
static int history_count = 0;
static int view_start_row = 0;

static int out_col = 0;
static char input_buf[INPUT_MAX];
static int input_len = 0;
static int is_focused = 0;

// 跨翻译单元共享状态
char current_dir[INPUT_MAX];
int sudo_mode = 0;

// 命令历史（最新在索引 0）
#define CMD_HISTORY_MAX 32
static char cmd_history[CMD_HISTORY_MAX][INPUT_MAX];
static int cmd_history_count = 0;
static int cmd_history_pos = 0;  // 0 = 当前/新输入，1 = 最近一条，...
// 上翻历史前正在输入的草稿；下键回到 0 时恢复
static char cmd_draft[INPUT_MAX];
static int cmd_draft_len = 0;
static int cmd_draft_saved = 0;  // 1 表示草稿已快照

void main();

__attribute__((naked)) void _start() {
    __asm__ volatile (
        "call main \n\t"
        "call exit \n\t"
        :
        :
        : "memory"
    );
}

void clear_output(void) {
    history_count = 0;
    view_start_row = 0;
    out_col = 0;
    // 初始化第一行
    history[0][0] = '\0';
    history_count = 1;
}

void push_char(char ch) {
    if (ch == '\r') return;

    int current_row = history_count - 1;

    if (ch == '\n') {
        out_col = 0;
        if (history_count < HISTORY_MAX) {
            history_count++;
            history[history_count - 1][0] = '\0';
        } else {
            // 历史记录满了，整体上移
            for (int i = 1; i < HISTORY_MAX; i++) {
                for (int j = 0; j <= HISTORY_MAX_COLS; j++) {
                    history[i - 1][j] = history[i][j];
                }
            }
            history[HISTORY_MAX - 1][0] = '\0';
        }

        // 如果原本在最底部，则自动滚动跟随
        int max_view = history_count - term_output_rows;
        if (max_view < 0) max_view = 0;

        // 允许有一行误差的跟随
        if (view_start_row >= max_view - 1) {
            view_start_row = max_view;
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

    if (out_col >= term_cols) {
        push_char('\n');
        current_row = history_count - 1; // 重新获取行号
    }

    history[current_row][out_col++] = ch;
    history[current_row][out_col] = '\0';
}

void write_text(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) push_char(s[i]);
}

void write_line(const char* s) {
    write_text(s);
    push_char('\n');
}

static void build_prompt(char* out, int max) {
    if (!out || max <= 0) return;

    out[0] = '\0';
    if (sudo_mode) {
        strlcpy(out, "root ", max);
    }

    if (current_dir[0]) {
        strlcat(out, "/", max);
        strlcat(out, current_dir, max);
    } else {
        strlcat(out, "/", max);
    }

    strlcat(out, " > ", max);
}

// 由真实窗口客户区尺寸重算布局全局量。
// 查询失败则回退默认值。列数变化后，已有历史行按新列数渲染
// （draw_text 会裁剪）；底层缓冲按 HISTORY_MAX_COLS 分配。
static void recompute_layout(void) {
    int w, h;
    if (!win_get_size(&w, &h) || w <= 0 || h <= 0) {
        // 查询失败则用默认值
        w = TERM_DEFAULT_W;
        h = TERM_DEFAULT_H;
    }
    term_client_w = w;
    term_client_h = h;

    int new_cols = (term_client_w - TERM_SCROLL_W - TERM_PADDING * 2) / 8;
    if (new_cols < 8) new_cols = 8;
    if (new_cols > HISTORY_MAX_COLS) new_cols = HISTORY_MAX_COLS;
    term_cols = new_cols;

    term_input_w = term_client_w - 20;

    term_input_y = term_client_h - TERM_INPUT_H - 10;
    if (term_input_y < 40) term_input_y = 40;

    term_output_rows = (term_input_y - TERM_PADDING) / TERM_ROW_HEIGHT;
    if (term_output_rows < 1) term_output_rows = 1;

    // 行数变化后把视图滚回有效范围
    int max_view = history_count - term_output_rows;
    if (max_view < 0) max_view = 0;
    if (view_start_row > max_view) view_start_row = max_view;
}

void render(void) {
    int accent = is_focused ? 9 : 8;

    // 使用真实客户区尺寸，避免右侧/底部被窗口裁掉
    draw_rect(0, 0, term_client_w, term_client_h, 0);
    draw_rect(0, 0, term_client_w, 1, accent);

    // 绘制历史记录文本
    for (int r = 0; r < term_output_rows; r++) {
        int history_idx = view_start_row + r;
        if (history_idx < history_count) {
            draw_text(TERM_PADDING, TERM_PADDING + r * TERM_ROW_HEIGHT, history[history_idx], 10);
        }
    }

    // 绘制滑块 (UI库)
    int slider_x = term_client_w - TERM_SCROLL_W;
    int slider_y = 2;
    int slider_w = TERM_SCROLL_W;
    int slider_h = term_input_y - slider_y - 2;

    int max_view = history_count - term_output_rows;
    if (max_view < 0) max_view = 0;

    ui_draw_slider(slider_x, slider_y, slider_w, slider_h, view_start_row, max_view, 1);

    // 绘制输入行（无框）：提示符 + 输入内容 + 块状光标
    char prompt[INPUT_MAX + 8];
    int p = 0;
    char prefix[INPUT_MAX];
    build_prompt(prefix, sizeof(prefix));
    int prefix_len = (int)strlen(prefix);

    for (int i = 0; i < prefix_len && p < (int)sizeof(prompt) - 1; i++) {
        prompt[p++] = prefix[i];
    }

    int max_visible = ((term_input_w - 8) / 8) - 1 - prefix_len;
    if (max_visible < 1) max_visible = 1;
    int start = (input_len > max_visible) ? (input_len - max_visible) : 0;

    for (int i = start; i < input_len && p < INPUT_MAX; i++) {
        prompt[p++] = input_buf[i];
    }
    prompt[p] = '\0';

    int text_y = term_input_y + 8;  // 与原输入框文字基线一致：(h - 12) / 2
    draw_text_rgb(TERM_INPUT_X + 8, text_y, prompt, TSUKI_RGB_TEXT);

    // 块状光标：仅聚焦时绘制，随 ticks 闪烁（25 ticks ≈ 250ms）
    if (is_focused && ((get_ticks() / 25) & 1u)) {
        int cursor_x = TERM_INPUT_X + 8 + measure_text(prompt);
        if (cursor_x + 8 > TERM_INPUT_X + term_input_w) {
            cursor_x = TERM_INPUT_X + term_input_w - 8;
        }
        draw_rect(cursor_x, text_y - 1, 8, TSUKI_FONT_HEIGHT + 2, 9);
    }
}

void main() {
    set_sandbox(1);
    win_set_title("terminal.tsk");

    // 首次渲染前查询真实窗口尺寸并计算布局
    recompute_layout();

    clear_output();
    input_buf[0] = '\0';
    input_len = 0;
    write_line("TSK terminal ready.");
    write_line("Type help for commands.");
    write_line("Hidden files require sudo.");

    is_focused = win_is_focused();
    render();

    while (1) {
        sleep(1);

        // 检测窗口缩放，必要时重算布局
        {
            static int last_w = 0, last_h = 0;
            int cur_w, cur_h;
            if (win_get_size(&cur_w, &cur_h)) {
                if (last_w == 0) { last_w = cur_w; last_h = cur_h; } // 首次
                if (cur_w != last_w || cur_h != last_h) {
                    last_w = cur_w;
                    last_h = cur_h;
                    recompute_layout();
                    render();
                }
            }
        }

        // 光标闪烁：相位翻转时重绘（无输入事件时光标也在呼吸）
        {
            static int last_blink = -1;
            int blink = (int)((get_ticks() / 25) & 1u);
            if (blink != last_blink) {
                last_blink = blink;
                if (is_focused) render();
            }
        }

        int mx, my;
        if (get_mouse_click(&mx, &my)) {
            // 检查是不是点击了滑块区域
            int slider_x = term_client_w - TERM_SCROLL_W;
            int slider_y = 2;
            int slider_w = TERM_SCROLL_W;
            int slider_h = term_input_y - slider_y - 2;

            if (ui_is_clicked(mx, my, slider_x, slider_y, slider_w, slider_h)) {
                // 简单比例映射：滑块内部点击 Y 坐标 → view_start_row
                int max_view = history_count - term_output_rows;
                if (max_view > 0) {
                    int relative_y = my - slider_y;
                    view_start_row = (relative_y * max_view) / slider_h;

                    if (view_start_row < 0) view_start_row = 0;
                    if (view_start_row > max_view) view_start_row = max_view;
                    render();
                }
            }
        }

        int ev = win_get_event();
        if (ev & WIN_EVENT_FOCUS_CHANGED) {
            is_focused = win_is_focused();
            render();
        }

        if (!(ev & WIN_EVENT_KEY_READY)) continue;

        int key = get_key();
        if (!key) continue;

        // 方向键: 命令历史导航
        if (key == KEY_UP) {
            if (cmd_history_pos < cmd_history_count) {
                // 首次从 pos 0 上移时，保存当前输入为 draft
                if (cmd_history_pos == 0) {
                    strlcpy(cmd_draft, input_buf, INPUT_MAX);
                    cmd_draft_len = input_len;
                    cmd_draft_saved = 1;
                }
                cmd_history_pos++;
                strlcpy(input_buf, cmd_history[cmd_history_pos - 1], INPUT_MAX);
                input_len = (int)strlen(input_buf);
            }
            render();
            continue;
        }

        if (key == KEY_DOWN) {
            if (cmd_history_pos > 0) {
                cmd_history_pos--;
                if (cmd_history_pos == 0) {
                    // 恢复 draft
                    if (cmd_draft_saved) {
                        strlcpy(input_buf, cmd_draft, INPUT_MAX);
                        input_len = cmd_draft_len;
                        cmd_draft_saved = 0;
                    } else {
                        input_len = 0;
                        input_buf[0] = '\0';
                    }
                } else {
                    strlcpy(input_buf, cmd_history[cmd_history_pos - 1], INPUT_MAX);
                    input_len = (int)strlen(input_buf);
                }
            }
            render();
            continue;
        }

        // 左右方向键: 暂不处理 (无行内光标移动)
        if (key == KEY_LEFT || key == KEY_RIGHT) continue;

        if (key == '\n') {
            {
                char prefix[INPUT_MAX];
                build_prompt(prefix, sizeof(prefix));
                write_text(prefix);
            }
            write_text(input_buf);
            push_char('\n');
            run_command(input_buf);

            // 保存到命令历史 (非空命令)
            if (input_len > 0) {
                // 去重 (与最新历史相同则跳过)
                if (cmd_history_count == 0 ||
                    strcmp(input_buf, cmd_history[0]) != 0) {
                    // 整体后移
                    for (int i = CMD_HISTORY_MAX - 1; i > 0; i--) {
                        strlcpy(cmd_history[i], cmd_history[i - 1], INPUT_MAX);
                    }
                    strlcpy(cmd_history[0], input_buf, INPUT_MAX);
                    if (cmd_history_count < CMD_HISTORY_MAX) cmd_history_count++;
                }
            }
            // 重置历史位置与 draft
            cmd_history_pos = 0;
            cmd_draft_saved = 0;
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
