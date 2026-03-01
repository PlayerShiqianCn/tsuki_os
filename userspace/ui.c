#include "ui.h"
#include "lib.h"

// 常见颜色
#define C_BLACK      0
#define C_WHITE      15
#define C_LIGHT_GRAY 7
#define C_DARK_GRAY  8
#define C_BLUE       1
#define C_LIGHT_BLUE 9
#define C_RED        4

int ui_is_clicked(int mx, int my, int x, int y, int w, int h) {
    if (mx >= x && mx <= x + w && my >= y && my <= y + h) {
        return 1;
    }
    return 0;
}

void ui_draw_button(int x, int y, int w, int h, const char* text, int bg, int fg, int pressed) {
    if (pressed) {
        // 按下状态：背景颜色加深，或者边框反转
        draw_rect(x, y, w, h, C_DARK_GRAY);
        draw_rect(x + 1, y + 1, w - 2, h - 2, bg);
        
        // 文本偏移一点点产生按下效果
        int tw = 0;
        int i = 0;
        while (text[i++]) tw += 8;
        int tx = x + (w - tw) / 2 + 1;
        int ty = y + (h - 8) / 2 + 1;
        draw_text(tx, ty, text, fg);
    } else {
        // 正常状态
        draw_rect(x, y, w, h, C_DARK_GRAY); // 边框
        draw_rect(x + 1, y + 1, w - 2, h - 2, bg);
        
        // 左上角的高光 (简单的 3D 效果)
        draw_rect(x, y, w - 1, 1, C_WHITE);
        draw_rect(x, y, 1, h - 1, C_WHITE);
        
        // 右下角的阴影
        draw_rect(x + 1, y + h - 1, w - 1, 1, C_BLACK);
        draw_rect(x + w - 1, y + 1, 1, h - 1, C_BLACK);
        
        // 居中文本
        int tw = 0;
        int i = 0;
        while (text[i++]) tw += 8;
        int tx = x + (w - tw) / 2;
        int ty = y + (h - 8) / 2;
        draw_text(tx, ty, text, fg);
    }
}

void ui_draw_input(int x, int y, int w, int h, const char* text, int focused) {
    // 外边框
    draw_rect(x, y, w, h, focused ? C_LIGHT_BLUE : C_DARK_GRAY);
    // 内背景
    draw_rect(x + 1, y + 1, w - 2, h - 2, C_WHITE);
    
    // 内阴影
    if (focused) {
        draw_rect(x + 1, y + 1, w - 2, 1, C_LIGHT_GRAY);
        draw_rect(x + 1, y + 1, 1, h - 2, C_LIGHT_GRAY);
    }
    
    // 文本
    if (text) {
        draw_text(x + 4, y + (h - 8) / 2, text, C_BLACK);
    }
    
    // 光标
    if (focused) {
        int tw = 0;
        int i = 0;
        if (text) {
            while (text[i++]) tw += 8;
        }
        draw_rect(x + 4 + tw, y + (h - 10) / 2, 2, 10, C_BLACK);
    }
}

void ui_draw_slider(int x, int y, int w, int h, int value, int max_value, int is_vertical) {
    // 1. 画轨道 (Track)
    draw_rect(x, y, w, h, C_LIGHT_GRAY);
    draw_rect(x, y, w, 1, C_DARK_GRAY);
    draw_rect(x, y, 1, h, C_DARK_GRAY);
    
    // 防御性检查
    if (max_value <= 0) max_value = 1;
    if (value < 0) value = 0;
    if (value > max_value) value = max_value;
    
    // 2. 画滑块 (Thumb)
    if (is_vertical) {
        // 垂直滑块
        int thumb_h = 20; // 滑块固定高度
        if (thumb_h > h) thumb_h = h;
        
        int track_h = h - thumb_h;
        int thumb_y = y + (value * track_h) / max_value;
        
        // 滑块本体
        draw_rect(x + 1, thumb_y, w - 2, thumb_h, C_WHITE);
        
        // 滑块边框
        draw_rect(x + 1, thumb_y, w - 2, 1, C_DARK_GRAY);
        draw_rect(x + 1, thumb_y + thumb_h - 1, w - 2, 1, C_BLACK);
        
        // 滑块上的防滑纹理 (3条线)
        int cy = thumb_y + thumb_h / 2;
        draw_rect(x + 3, cy - 2, w - 6, 1, C_LIGHT_GRAY);
        draw_rect(x + 3, cy, w - 6, 1, C_LIGHT_GRAY);
        draw_rect(x + 3, cy + 2, w - 6, 1, C_LIGHT_GRAY);
        
    } else {
        // 水平滑块
        int thumb_w = 20; // 滑块固定宽度
        if (thumb_w > w) thumb_w = w;
        
        int track_w = w - thumb_w;
        int thumb_x = x + (value * track_w) / max_value;
        
        // 滑块本体
        draw_rect(thumb_x, y + 1, thumb_w, h - 2, C_WHITE);
        
        // 滑块边框
        draw_rect(thumb_x, y + 1, 1, h - 2, C_DARK_GRAY);
        draw_rect(thumb_x + thumb_w - 1, y + 1, 1, h - 2, C_BLACK);
        
        // 滑块上的防滑纹理 (3条线)
        int cx = thumb_x + thumb_w / 2;
        draw_rect(cx - 2, y + 3, 1, h - 6, C_LIGHT_GRAY);
        draw_rect(cx, y + 3, 1, h - 6, C_LIGHT_GRAY);
        draw_rect(cx + 2, y + 3, 1, h - 6, C_LIGHT_GRAY);
    }
}
