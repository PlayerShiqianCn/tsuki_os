#include "window.h"
#include "video.h"
#include "utils.h"
#include "heap.h"

// 声明外部依赖
extern void put_pixel(int x, int y, unsigned char color);

static Window* layers[MAX_LAYERS];
static int win_count = 0;

// 拖拽相关
static int is_dragging = 0;
static Window* drag_win = 0;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

void win_init() {
    win_count = 0;
    is_dragging = 0;
    for(int i=0; i<MAX_LAYERS; i++) layers[i] = 0;
}

void win_bring_to_front(Window* w) {
    if (win_count <= 1) return;
    if (layers[win_count-1] == w) return;

    int idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (layers[i] == w) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;

    for (int i = idx; i < win_count - 1; i++) {
        layers[i] = layers[i+1];
    }
    layers[win_count - 1] = w;
}

void win_destroy(Window* w) {
    if (!w) return;

    int idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (layers[i] == w) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;

    for (int i = idx; i < win_count - 1; i++) {
        layers[i] = layers[i+1];
    }
    
    layers[win_count - 1] = 0;
    win_count--;

    if (is_dragging && drag_win == w) {
        is_dragging = 0;
        drag_win = 0;
    }

    if (w->title) free(w->title);
    
    // 【修正 1】释放 buffer
    if (w->buffer) free(w->buffer);
    
    free(w);
}

Window* win_create(int x, int y, int w, int h, char* title, unsigned char color) {
    if (win_count >= MAX_LAYERS) return 0;

    Window* new_win = (Window*)malloc(sizeof(Window));
    if (!new_win) return 0;

    new_win->id = win_count; 
    new_win->x = x;
    new_win->y = y;
    new_win->w = w;
    new_win->h = h;
    new_win->visible = 1;
    new_win->bg_color = color;

    // 【修正 2】统一只使用 buffer
    int buf_size = w * h;
    new_win->buffer = (unsigned char*)malloc(buf_size);
    
    if (new_win->buffer) {
        // 初始化显存为背景色 (关键：防止花屏)
        memset(new_win->buffer, color, buf_size);
    } else {
        free(new_win);
        return 0;
    }
    
    // 注意：back_buffer 及其相关代码已被完全移除

    int title_len = strlen(title);
    new_win->title = (char*)malloc(title_len + 1);
    strcpy(new_win->title, title);
    
    new_win->extra_draw = 0; 
    layers[win_count] = new_win;
    win_count++;

    return new_win;
}

// 【修正 3】写入 buffer
void win_put_pixel(Window* w, int x, int y, unsigned char color) {
    if (!w || !w->buffer) return;
    
    // 边界检查
    if (x >= 0 && x < w->w && y >= 0 && y < w->h) {
        w->buffer[y * w->w + x] = color;
    }
}

// 【修正 4】读取 buffer (与 win_put_pixel 对应)
unsigned char win_get_pixel(Window* w, int x, int y) {
    if (!w || !w->buffer) return 0;
    
    if (x >= 0 && x < w->w && y >= 0 && y < w->h) {
        return w->buffer[y * w->w + x];
    }
    return 0;
}

void win_draw_all() {
    for (int i = 0; i < win_count; i++) {
        Window* w = layers[i];
        if (!w->visible) continue;

        // 1. 画阴影 (偏移 3px)
        draw_rect(w->x + 3, w->y + 3, w->w, w->h, C_DARK_GRAY);
        
        // 2. 画边框
        draw_rect(w->x - BORDER_WIDTH, w->y - BORDER_WIDTH, 
                  w->w + BORDER_WIDTH*2, w->h + BORDER_WIDTH*2, C_WHITE);
        
        // 3. 【修正 5】将 buffer 的内容画到屏幕上
        // 这会画出背景色以及 App 绘制的方块
        if (w->buffer) {
            for (int row = 0; row < w->h; row++) {
                for (int col = 0; col < w->w; col++) {
                    // 读取 buffer
                    unsigned char color = w->buffer[row * w->w + col];
                    // 写入屏幕显存 (video.c 的 put_pixel)
                    put_pixel(w->x + col, w->y + row, color);
                }
            }
        } else {
            // 降级处理
            draw_rect(w->x, w->y, w->w, w->h, w->bg_color);
        }

        // 4. 画标题栏 (画在 buffer 之上，确保不被 buffer 里的背景色覆盖)
        // 只有当前拖拽的窗口才高亮标题栏 (简单的焦点模拟)
        int title_color = (is_dragging && drag_win == w) ? C_LIGHT_BLUE : C_BLUE;
        
        draw_rect(w->x, w->y, w->w, TITLE_BAR_HEIGHT, title_color);
        draw_string(w->x + 4, w->y + (TITLE_BAR_HEIGHT - 8)/2, w->title, C_WHITE);
        
        // 5. 画关闭按钮
        int btn_size = 11;
        int btn_y = w->y + (TITLE_BAR_HEIGHT - btn_size)/2;
        draw_rect(w->x + w->w - 14, btn_y, btn_size, btn_size, C_WHITE);
        draw_rect(w->x + w->w - 13, btn_y + 1, btn_size - 2, btn_size - 2, C_RED);

        // 6. 额外的回调绘制 (例如 Console 可能会在这里画字)
        if (w->extra_draw) {
            w->extra_draw(w); 
        }
    }
}

// 鼠标处理保持不变...
void win_handle_mouse(ps2_mouse_event_t* event, int mx, int my) {
    int left_btn = event->buttons & 1;

    if (left_btn) {
        if (!is_dragging) {
            // 从最上层窗口开始检测
            for (int i = win_count - 1; i >= 0; i--) {
                Window* w = layers[i];
                if (!w->visible) continue;

                // 简单的矩形碰撞检测 (包括了标题栏区域，因为 w->y 指的是窗口内容左上角吗？
                // 通常 win->y 是窗口整体左上角，或者内容左上角，需要根据你的定义。
                // 假设 w->y 是内容左上角，标题栏在 y - TITLE_BAR_HEIGHT。
                // 但看你的 draw_rect 逻辑：draw_rect(w->x, w->y, ... TITLE_BAR_HEIGHT)
                // 这说明 w->x, w->y 就是窗口的左上角（包含标题栏）
                
                if (mx >= w->x && mx <= w->x + w->w &&
                    my >= w->y && my <= w->y + w->h) {
                    
                    // 检查关闭按钮
                    int btn_x = w->x + w->w - 14;
                    int btn_y = w->y + (TITLE_BAR_HEIGHT - 11)/2; // 近似计算
                    int btn_size = 11;

                    if (mx >= btn_x && mx <= btn_x + btn_size &&
                        my >= btn_y && my <= btn_y + btn_size) {
                        win_destroy(w);
                        return;
                    }

                    win_bring_to_front(w);
                    
                    // 检查是否点在标题栏区域
                    if (my <= w->y + TITLE_BAR_HEIGHT) {
                        is_dragging = 1;
                        drag_win = w;
                        drag_offset_x = mx - w->x;
                        drag_offset_y = my - w->y;
                    }
                    return; 
                }
            }
        } else {
            if (drag_win) {
                drag_win->x = mx - drag_offset_x;
                drag_win->y = my - drag_offset_y;
            }
        }
    } else {
        is_dragging = 0;
        drag_win = 0;
    }
}

int win_get_count() {
    return win_count;
}

Window* win_get_at_layer(int index) {
    if (index < 0 || index >= win_count) return 0;
    return layers[index];
}