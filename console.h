#ifndef CONSOLE_H
#define CONSOLE_H

#include "window.h"

// 终端配置
#define CONSOLE_ROWS 10
#define CONSOLE_COLS 30

typedef struct {
    Window* win;            // 关联的窗口
    char buffer[CONSOLE_ROWS][CONSOLE_COLS + 1]; // 屏幕显示缓冲
    char input_buf[64];     // 当前正在输入的命令
    int input_len;
    int current_line;       // 当前行号
} Console;

void console_init();
void console_handle_key(char c);
void console_draw(); // 专门的刷新函数
void console_render(Window* w); // 窗口专用渲染函数
void console_write(const char* str);
int console_launch_tsk(const char* filename);
#endif
