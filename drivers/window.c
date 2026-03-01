#include "window.h"
#include "mp.h"
#include "video.h"
#include "utils.h"
#include "process.h"
#include "klog.h"

#define WINDOW_BUFFER_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)

static Window* layers[MAX_LAYERS];
static int win_count = 0;
static Window window_pool[MAX_LAYERS];
static unsigned char window_used[MAX_LAYERS];
static char window_titles[MAX_LAYERS][32];
static unsigned char window_buffer_used[MP_WINDOW_BUFFER_SLOTS];

static int is_dragging = 0;
static Window* drag_win = 0;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

static unsigned int* window_buffer_ptr(int slot) {
    return ((unsigned int*)MP_WINDOW_BUFFER_BASE) + slot * WINDOW_BUFFER_PIXELS;
}

static int alloc_window_buffer_slot(void) {
    for (int i = 0; i < MP_WINDOW_BUFFER_SLOTS; i++) {
        if (!window_buffer_used[i]) {
            window_buffer_used[i] = 1;
            return i;
        }
    }
    return -1;
}

static void release_window_buffer_slot(int slot) {
    if (slot < 0 || slot >= MP_WINDOW_BUFFER_SLOTS) return;
    window_buffer_used[slot] = 0;
}

void win_init() {
    win_count = 0;
    is_dragging = 0;
    for (int i = 0; i < MAX_LAYERS; i++) {
        layers[i] = 0;
        window_used[i] = 0;
        window_titles[i][0] = '\0';
    }
    for (int i = 0; i < MP_WINDOW_BUFFER_SLOTS; i++) {
        window_buffer_used[i] = 0;
    }
    video_request_redraw();
}

void win_bring_to_front(Window* w) {
    if (win_count <= 1) return;
    if (layers[win_count - 1] == w) return;

    int idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (layers[i] == w) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;

    for (int i = idx; i < win_count - 1; i++) {
        layers[i] = layers[i + 1];
    }
    layers[win_count - 1] = w;
    video_request_redraw();
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
        layers[i] = layers[i + 1];
    }

    layers[win_count - 1] = 0;
    win_count--;

    if (is_dragging && drag_win == w) {
        is_dragging = 0;
        drag_win = 0;
    }

    release_window_buffer_slot(w->buffer_slot);

    for (int i = 0; i < MAX_LAYERS; i++) {
        if (&window_pool[i] == w) {
            window_used[i] = 0;
            window_titles[i][0] = '\0';
            break;
        }
    }

    video_request_redraw();
}

Window* win_create(int x, int y, int w, int h, char* title, unsigned char color) {
    if (win_count >= MAX_LAYERS) {
        klog_write("win slots full");
        return 0;
    }

    Window* new_win = 0;
    int slot = -1;
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (!window_used[i]) {
            slot = i;
            window_used[i] = 1;
            new_win = &window_pool[i];
            memset(new_win, 0, sizeof(Window));
            break;
        }
    }
    if (!new_win) {
        klog_write("win struct oom");
        return 0;
    }

    if (w <= 0 || h <= 0 || w * h > WINDOW_BUFFER_PIXELS) {
        klog_write("win too big");
        window_used[slot] = 0;
        return 0;
    }

    int buf_slot = alloc_window_buffer_slot();
    if (buf_slot < 0) {
        klog_write("win buf slots full");
        window_used[slot] = 0;
        return 0;
    }

    new_win->id = win_count;
    new_win->x = x;
    new_win->y = y;
    new_win->w = w;
    new_win->h = h;
    new_win->visible = 1;
    new_win->borderless = 0;
    new_win->bg_color = color;
    new_win->buffer_slot = buf_slot;
    new_win->buffer = window_buffer_ptr(buf_slot);

    {
        unsigned int fill = video_color_to_rgb(color);
        int buf_size = w * h;
        for (int i = 0; i < buf_size; i++) {
            new_win->buffer[i] = fill;
        }
    }

    if (!title) title = "tsk";
    {
        int i = 0;
        while (i < (int)sizeof(window_titles[slot]) - 1 && title[i]) {
            window_titles[slot][i] = title[i];
            i++;
        }
        window_titles[slot][i] = '\0';
    }
    new_win->title = window_titles[slot];

    new_win->extra_draw = 0;
    layers[win_count] = new_win;
    win_count++;
    video_request_redraw();

    return new_win;
}

void win_put_pixel(Window* w, int x, int y, unsigned int color) {
    if (!w || !w->buffer) return;
    if (x >= 0 && x < w->w && y >= 0 && y < w->h) {
        w->buffer[y * w->w + x] = color & 0x00FFFFFFu;
    }
}

unsigned int win_get_pixel(Window* w, int x, int y) {
    if (!w || !w->buffer) return 0;
    if (x >= 0 && x < w->w && y >= 0 && y < w->h) {
        return w->buffer[y * w->w + x];
    }
    return 0;
}

int win_set_title(Window* w, const char* title) {
    if (!w || !title) return 0;
    if (!w->title) return 0;

    {
        int i = 0;
        while (i < 31 && title[i]) {
            w->title[i] = title[i];
            i++;
        }
        w->title[i] = '\0';
    }
    video_request_redraw();
    return 1;
}

void win_draw_all() {
    for (int i = 0; i < win_count; i++) {
        Window* w = layers[i];
        if (!w->visible) continue;

        if (!w->borderless) {
            draw_rect(w->x + 3, w->y + 3, w->w, w->h, C_DARK_GRAY);
            draw_rect(w->x - BORDER_WIDTH, w->y - BORDER_WIDTH,
                      w->w + BORDER_WIDTH * 2, w->h + BORDER_WIDTH * 2, C_WHITE);
        }

        if (w->buffer) {
            for (int row = 0; row < w->h; row++) {
                for (int col = 0; col < w->w; col++) {
                    unsigned int color = w->buffer[row * w->w + col];
                    put_pixel_rgb(w->x + col, w->y + row, color);
                }
            }
        } else {
            draw_rect(w->x, w->y, w->w, w->h, w->bg_color);
        }

        if (!w->borderless) {
            int focused = (i == win_count - 1);
            int title_color = focused ? C_LIGHT_BLUE : C_BLUE;

            draw_rect(w->x, w->y, w->w, TITLE_BAR_HEIGHT, title_color);
            draw_string(w->x + 4, w->y + (TITLE_BAR_HEIGHT - 8) / 2, w->title, C_WHITE);

            {
                int btn_size = 11;
                int btn_y = w->y + (TITLE_BAR_HEIGHT - btn_size) / 2;
                draw_rect(w->x + w->w - 14, btn_y, btn_size, btn_size, C_WHITE);
                draw_rect(w->x + w->w - 13, btn_y + 1, btn_size - 2, btn_size - 2, C_RED);
            }
        }

        if (w->extra_draw) {
            w->extra_draw(w);
        }
    }
}

void win_handle_mouse(ps2_mouse_event_t* event, int mx, int my) {
    int left_btn = event->buttons & 1;

    if (left_btn) {
        if (!is_dragging) {
            for (int i = win_count - 1; i >= 0; i--) {
                Window* w = layers[i];
                if (!w->visible) continue;

                if (mx >= w->x && mx <= w->x + w->w &&
                    my >= w->y && my <= w->y + w->h) {

                    if (!w->borderless) {
                        int btn_x = w->x + w->w - 14;
                        int btn_y = w->y + (TITLE_BAR_HEIGHT - 11) / 2;
                        int btn_size = 11;

                        if (mx >= btn_x && mx <= btn_x + btn_size &&
                            my >= btn_y && my <= btn_y + btn_size) {
                            Process* owner = process_find_by_window(w);
                            if (owner && owner->pid != 0) {
                                owner->state = PROCESS_DEAD;
                                owner->win = 0;
                            }
                            win_destroy(w);
                            return;
                        }
                    }

                    win_bring_to_front(w);

                    if (!w->borderless && my <= w->y + TITLE_BAR_HEIGHT) {
                        is_dragging = 1;
                        drag_win = w;
                        drag_offset_x = mx - w->x;
                        drag_offset_y = my - w->y;
                    } else {
                        Process* owner = process_find_by_window(w);
                        if (owner && owner->pid != 0) {
                            if (w->borderless) {
                                owner->mouse_click_x = mx - w->x;
                                owner->mouse_click_y = my - w->y;
                            } else {
                                owner->mouse_click_x = mx - w->x - BORDER_WIDTH;
                                owner->mouse_click_y = my - w->y - TITLE_BAR_HEIGHT;
                            }
                            owner->has_mouse_event = 1;
                        }
                    }
                    return;
                }
            }
        } else {
            if (drag_win) {
                int new_x = mx - drag_offset_x;
                int new_y = my - drag_offset_y;
                if (drag_win->x != new_x || drag_win->y != new_y) {
                    drag_win->x = new_x;
                    drag_win->y = new_y;
                    video_request_redraw();
                }
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

Window* win_get_focused() {
    if (win_count <= 0) return 0;
    return layers[win_count - 1];
}
