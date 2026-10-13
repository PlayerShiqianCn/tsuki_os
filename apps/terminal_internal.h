#ifndef TERMINAL_INTERNAL_H
#define TERMINAL_INTERNAL_H

/*
 * 终端内部共享头 —— 仅限终端自己的翻译单元使用。
 *
 * 拆分布局：
 *   terminal_tsk.c  外壳：入口、事件循环、布局渲染、输出缓冲
 *   terminal_cmd.c  命令层：命令分发、各命令实现、路径/网络工具
 */

#include "lib.h"
#include "ui.h"
#include "theme.h"
#include "string.h"

/* ==== 布局常量 ==== */
#define TERM_PADDING 10
#define TERM_SCROLL_W 20
#define TERM_INPUT_X 10
#define TERM_INPUT_H 28
#define TERM_ROW_HEIGHT 14
#define INPUT_MAX 64
#define FILE_BUF_SIZE 2048
#define NET_HTTP_BUF_SIZE 1536
#define HISTORY_MAX 100
#define HISTORY_MAX_COLS 120
#define HIDDEN_SUFFIX "._hid_"

/* 默认尺寸（首次 win_get_size 失败或之前的回退值） */
#define TERM_DEFAULT_W 518
#define TERM_DEFAULT_H 271

/* ==== 跨翻译单元共享状态（定义在 terminal_tsk.c） ==== */
extern char current_dir[INPUT_MAX];  /* 相对根目录的当前目录，空串表示根 */
extern int sudo_mode;                /* 0 = 普通，1 = sudo 模式（允许隐藏文件） */

/* ==== 输出层（定义在 terminal_tsk.c，命令层调用） ==== */
void clear_output(void);
void push_char(char ch);
void write_text(const char* s);
void write_line(const char* s);
void render(void);

/* ==== 命令层（定义在 terminal_cmd.c，外壳调用） ==== */
void run_command(const char* raw_cmd);

#endif
