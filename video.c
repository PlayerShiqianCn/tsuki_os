#include "video.h"
#include "utils.h"
#include "font8x8_basic.h" // 确保这个文件在目录下

// 后台缓冲区
static unsigned char back_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];

void video_init() {
    // 清空缓冲区
    memset(back_buffer, C_CYAN, SCREEN_WIDTH * SCREEN_HEIGHT);
}

void put_pixel(int x, int y, unsigned char color) {
    if(x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        back_buffer[y * SCREEN_WIDTH + x] = color;
    }
}

void draw_rect(int x, int y, int w, int h, unsigned char color) {
    for(int i = 0; i < h; i++) {
        for(int j = 0; j < w; j++) {
            put_pixel(x + j, y + i, color);
        }
    }
}

int get_font_index(char c) {
    unsigned char uc = (unsigned char)c;
    if (uc < 128) return (int)uc;
    return 0;
}

// video.c 部分代码
// ...
void draw_char(int x, int y, char c, unsigned char color) {
    int font_idx = get_font_index(c);
    for (int i = 0; i < 8; i++) {
        unsigned char row = (unsigned char)font8x8_basic[font_idx][i];
        for (int j = 0; j < 8; j++) {
            // 注意：这里使用的是 font8x8_basic.h 的标准逻辑
            // 如果你的字还是反的，请在这里调整逻辑，但通常标准库是 LSB 优先的
            if (((row >> j) & 1) != 0) {
                put_pixel(x + j, y + i, color);
            }
        }
    }
}


void draw_string(int x, int y, char* str, unsigned char color) {
    int i = 0;
    while(str[i] != 0) {
        draw_char(x + (i * 8), y, str[i], color);
        i++;
    }
}

void video_swap_buffer() {
    memcpy((void*)VGA_ADDRESS, back_buffer, SCREEN_WIDTH * SCREEN_HEIGHT);
}

void draw_pixel(int x, int y, unsigned char color) {
    put_pixel(x, y, color);
}