#ifndef WINDOW_H
#define WINDOW_H

#include "ps2.h"

#define MAX_LAYERS 20 // 支持的最大层叠深度
#define TITLE_BAR_HEIGHT 20
#define BORDER_WIDTH 2

typedef struct Window {
    int id; // 唯一ID
    int x, y;
    int w, h;
    char* title; // 改成指针，动态分配
    int visible;
    int borderless; // 无边框模式（无标题栏、无边框、无阴影）
    unsigned char bg_color;
    int buffer_slot;
    unsigned int* buffer;
    void (*extra_draw)(struct Window* win); 
} Window;

void win_init();
// 返回创建的窗口指针
Window* win_create(int x, int y, int w, int h, char* title, unsigned char color);
void win_draw_all();
void win_handle_mouse(ps2_mouse_event_t* event, int mouse_x, int mouse_y);
void win_destroy(Window* w); 
void win_bring_to_front(Window* w);
unsigned int win_get_pixel(Window* w, int x, int y);
int win_set_title(Window* w, const char* title);
int win_get_count();
Window* win_get_at_layer(int index);
Window* win_get_focused();

// 更新窗口缓冲区的像素
void win_put_pixel(Window* w, int x, int y, unsigned int color);

#endif
