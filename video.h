#ifndef VIDEO_H
#define VIDEO_H

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define FRAMEBUFFER_WIDTH 640
#define FRAMEBUFFER_HEIGHT 480
#define FRAMEBUFFER_MAX_WIDTH 1024
#define FRAMEBUFFER_MAX_HEIGHT 768
#define VGA_ADDRESS 0xA0000
#define VBE_LFB_ADDRESS 0xE0000000u

// 常用颜色
#define C_BLACK 0
#define C_BLUE  1
#define C_GREEN 2
#define C_CYAN  3
#define C_RED   4
#define C_MAGENTA 5
#define C_BROWN 6
#define C_LIGHT_GRAY 7
#define C_DARK_GRAY 8
#define C_LIGHT_BLUE 9
#define C_LIGHT_GREEN 10
#define C_LIGHT_CYAN 11
#define C_LIGHT_RED 12
#define C_LIGHT_MAGENTA 13
#define C_YELLOW 14
#define C_WHITE 15

void video_init();
void put_pixel(int x, int y, unsigned char color);
void put_pixel_rgb(int x, int y, unsigned int rgb);
void draw_pixel(int x, int y, unsigned char color);
void draw_rect(int x, int y, int w, int h, unsigned char color);
void draw_rect_rgb(int x, int y, int w, int h, unsigned int rgb);
unsigned char video_rgb_to_index(unsigned int rgb);
unsigned int video_color_to_rgb(unsigned char color);
void draw_char(int x, int y, char c, unsigned char color);
void draw_string(int x, int y, char* str, unsigned char color);
int video_set_resolution(int w, int h);
void video_get_resolution(int* w, int* h);
void video_request_redraw(void);
int video_consume_redraw(void);
void video_swap_buffer(); // 双缓冲交换

#endif
