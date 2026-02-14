#include "console.h"
#include "utils.h"
#include "video.h"
#include "fs.h"
#include "heap.h"
#include "process.h"
#include "window.h"

// 定义命令执行函数的类型 (用于 .tsk 跳转)
typedef void (*tsk_func_t)(void);

// 声明外部加载器函数
extern int tsk_load(const char* filename, void** entry_point);

// 全局 Console 实例
static Console console_inst;

// 辅助：清空所有缓冲
void console_clear() {
    for (int i = 0; i < CONSOLE_ROWS; i++) {
        memset(console_inst.buffer[i], 0, CONSOLE_COLS + 1);
    }
    console_inst.current_line = 0;
    console_inst.input_len = 0;
    memset(console_inst.input_buf, 0, sizeof(console_inst.input_buf));
}

// 辅助：向上滚动一行
void console_scroll() {
    // 将 1~N 行移到 0~N-1
    for (int i = 1; i < CONSOLE_ROWS; i++) {
        memcpy(console_inst.buffer[i - 1], console_inst.buffer[i], CONSOLE_COLS + 1);
    }
    // 清空最后一行
    memset(console_inst.buffer[CONSOLE_ROWS - 1], 0, CONSOLE_COLS + 1);
    console_inst.current_line = CONSOLE_ROWS - 1;
}

// 应用程序窗口的回调函数
// 修改 console.c

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
    // 注意：这里需要你根据实际的 Window 结构体来获取像素
    // 假设 w->buffer 是一个记录颜色的线性数组，或者有一个 win_get_pixel 函数
    
    for (int y = 0; y < client_h; y++) {
        for (int x = 0; x < client_w; x++) {
            // 获取窗口 buffer 中的颜色
            // 【方式 A】如果你有 win_get_pixel(Window* w, int x, int y)
            unsigned char color = win_get_pixel(w, x + BORDER_WIDTH, y + TITLE_BAR_HEIGHT);
            
            // 将颜色画到屏幕绝对坐标
            draw_pixel(start_x + x, start_y + y, color);
        }
    }
}

// 核心：向终端写入字符串
// 核心：向终端写入字符串
void console_write(const char* str) {
    if (!str) return;

    int i = 0;
    while (str[i]) {
        char c = str[i];

        if (c == '\n') {
            console_inst.current_line++;
            if (console_inst.current_line >= CONSOLE_ROWS) {
                console_scroll();
            }
        }
        // ================== 新增：处理退格键 ==================
        else if (c == '\b') {
            int len = strlen(console_inst.buffer[console_inst.current_line]);
            if (len > 0) {
                // 将字符串结束符前移一位，相当于删除最后一个字符
                console_inst.buffer[console_inst.current_line][len - 1] = '\0';
            }
        }
        // ====================================================
        else {
            // 找到当前行第一个空位置
            int len = strlen(console_inst.buffer[console_inst.current_line]);
            
            if (len < CONSOLE_COLS) {
                console_inst.buffer[console_inst.current_line][len] = c;
                console_inst.buffer[console_inst.current_line][len + 1] = '\0';
            } else {
                // 当前行满了，换行
                console_inst.current_line++;
                if (console_inst.current_line >= CONSOLE_ROWS) {
                    console_scroll();
                }
                console_inst.buffer[console_inst.current_line][0] = c;
                console_inst.buffer[console_inst.current_line][1] = '\0';
            }
        }
        i++;
    }
}
// 渲染回调函数：被 window 系统调用
void console_render(Window* w) {
    // 设置背景 (黑色) - 确保在标题栏下方
    draw_rect(w->x + BORDER_WIDTH, w->y + TITLE_BAR_HEIGHT,
              w->w - BORDER_WIDTH * 2, w->h - TITLE_BAR_HEIGHT - BORDER_WIDTH, C_BLACK);

    // 绘制 buffer 内容
    int start_x = w->x + BORDER_WIDTH + 2;
    int start_y = w->y + TITLE_BAR_HEIGHT + 2;
    int line_h = 10; // 行高

    for (int i = 0; i < CONSOLE_ROWS; i++) {
        draw_string(start_x, start_y + i * line_h, console_inst.buffer[i], C_GREEN);
    }
}

// 执行命令
void execute_command(char* cmd) {
    if (strlen(cmd) == 0) return;

    // 1. cls - 清屏
    if (strcmp(cmd, "cls") == 0) {
        console_clear();
        console_write("> ");
        return;
    }

    // 2. ls - 列出文件
    if (strcmp(cmd, "ls") == 0) {
        console_write("Listing files:\n");

        // 分配缓冲区获取文件列表
        char* file_list = (char*)malloc(1024);
        if (file_list) {
            memset(file_list, 0, 1024);
            fs_get_file_list(file_list, 1024);
            if (strlen(file_list) == 0) {
                console_write("(no files)\n");
            } else {
                console_write(file_list);
            }
            free(file_list);
        } else {
            console_write("Memory error\n");
        }
        return;
    }
    if (strcmp(cmd, "cat ") == 0 || strncmp(cmd, "cat ", 4) == 0) {
        char* filename = cmd + 4; // 跳过 "cat "
        
        // 跳过可能存在的额外空格
        while (*filename == ' ') filename++;
        
        if (*filename == '\0') {
            console_write("Usage: cat <filename>\n");
            return;
        }

        // 分配缓冲区 (例如 4KB，对于文本文件通常够用了)
        // 注意：如果你之前的 fs_read_file 有 12KB 限制，这里 buffer 也要够大
        int buf_size = 4096;
        char* buf = (char*)malloc(buf_size);
        
        if (!buf) {
            console_write("Error: Out of memory.\n");
            return;
        }

        // 清空缓冲区
        memset(buf, 0, buf_size);

        // 调用 fs_read_file
        console_write("Reading file: ");
        console_write(filename);
        console_write("...\n");

        int bytes_read = fs_read_file(filename, buf);

        if (bytes_read > 0) {
            // 确保字符串以 null 结尾，防止打印乱码
            if (bytes_read >= buf_size) {
                buf[buf_size - 1] = '\0'; 
            } else {
                buf[bytes_read] = '\0';
            }
            
            // 打印文件内容
            console_write("--- File Content Start ---\n");
            console_write(buf);
            console_write("\n--- File Content End ---\n");
        } else {
            console_write("Error: File not found or empty.\n");
        }

        // 释放内存
        free(buf);
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        console_write("Available commands:\n");
        console_write("cls - Clear the screen\n");
        console_write("ls  - List files\n");
        console_write("cat <filename> - Display file content\n");
        console_write("help - Show this help message\n");

        console_write("You can also run .tsk files.\n");
        return;
    }

    // 3. 执行 .tsk 文件
    int len = strlen(cmd);
    if (len > 4 && strcmp(cmd + len - 4, ".tsk") == 0) {
        console_write("Launching process...\n");

        void* entry_point = 0;

        // 1. 加载代码到内存
        if (tsk_load(cmd, &entry_point)) {

            // 2. 创建一个专属窗口
            Window* app_win = win_create(100, 80, 200, 150, cmd, C_BLACK);
            app_win->extra_draw = app_window_render;
            win_draw_all();

            // 3. 【关键修改】创建进程而不是直接调用
            process_create((void (*)())entry_point, cmd, app_win);

            // 强制重绘以显示App内容
            win_draw_all();

            console_write("Process started.\n");

        } else {
            console_write("Load failed.\n");
        }
        return;
    }

    // 未知命令
    console_write("Unknown command: ");
    console_write(cmd);
    console_write("\n");
}

void console_handle_key(char c) {
    // 1. 处理回车
    if (c == '\n') {
        console_write("\n"); // 换行
        execute_command(console_inst.input_buf); // 执行
        memset(console_inst.input_buf, 0, sizeof(console_inst.input_buf));
        console_inst.input_len = 0;
        console_write("> "); // 打印新提示符
    }
    // 2. 处理退格
    else if (c == '\b') {
        if (console_inst.input_len > 0) {
            console_inst.input_len--;
            console_inst.input_buf[console_inst.input_len] = '\0';
            // 在屏幕上删除最后一个字符（简单处理）
            console_write("\b \b"); // 光标回退，覆盖为空格，再回退
        }
    }
    // 3. 普通字符
    else {
        if (console_inst.input_len < 60) { // 防止溢出
            console_inst.input_buf[console_inst.input_len++] = c;
            console_inst.input_buf[console_inst.input_len] = '\0';
            char tmp[2] = {c, '\0'};
            console_write(tmp); // 回显到屏幕
        }
    }
    // 重绘窗口以显示更新
    win_draw_all();
}

void console_init() {
    console_clear();
    
    // 创建一个窗口，居中
    console_inst.win = win_create(40, 40, 240, 140, "Terminal", C_BLACK);
    console_inst.win->extra_draw = console_render;
    
    // 打印欢迎信息
    console_write("MyOS v0.1 MultiTask\n");
    console_write("Type 'app.tsk' to run\n");
    console_write("> ");
}