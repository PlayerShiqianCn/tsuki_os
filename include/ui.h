#ifndef UI_H
#define UI_H

// 检查鼠标是否点击在指定区域内
int ui_is_clicked(int mx, int my, int x, int y, int w, int h);

// 绘制一个标准按钮
void ui_draw_button(int x, int y, int w, int h, const char* text, int bg, int fg, int pressed);

// 绘制一个输入框
void ui_draw_input(int x, int y, int w, int h, const char* text, int focused);

// 绘制一个滑块
// value: 当前值
// max_value: 最大值
// is_vertical: 1垂直，0水平
void ui_draw_slider(int x, int y, int w, int h, int value, int max_value, int is_vertical);

#endif
