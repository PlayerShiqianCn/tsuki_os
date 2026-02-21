#ifndef LIB_H
#define LIB_H

#include "syscall.h"

// API 声明
void exit();
void print(const char* str);
int read_file(const char* filename, void* buffer);
void draw_rect(int x, int y, int w, int h, int color);
void draw_text(int x, int y, const char* str, int color);
int get_key(void);
void set_sandbox(int level);
int win_create(int x, int y, int w, int h, const char* title);
int win_set_title(const char* title);
int win_is_focused(void);
int win_get_event(void);
int list_files(char* buffer, int max_len);
int launch_tsk(const char* filename);

#endif
