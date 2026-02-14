#ifndef LIB_H
#define LIB_H

// 系统调用号
#define SYS_EXIT        0
#define SYS_PRINT       1
#define SYS_READ_FILE   2
#define SYS_DRAW_RECT   3
#define SYS_SLEEP       4
#define SYS_SET_SANDBOX 5

// API 声明
void exit();
void print(const char* str);
int read_file(const char* filename, void* buffer);
void draw_rect(int x, int y, int w, int h, int color);
void set_sandbox(int level);

#endif