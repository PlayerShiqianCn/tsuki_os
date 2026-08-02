#include "window.h"
#include "mp.h"
#include "video.h"
#include "utils.h"
#include "process.h"
#include "klog.h"
#include "irq.h"
#include "kernel_core.h"

#define WINDOW_PAGE_SIZE 4096
#define WINDOW_ARENA_PAGES (MP_WINDOW_BUFFER_ARENA_SIZE / WINDOW_PAGE_SIZE)

static Window* layers[MAX_LAYERS];
static int win_count;
static Window window_pool[MAX_LAYERS];
static unsigned char window_used[MAX_LAYERS];
static char window_titles[MAX_LAYERS][32];
static SlotArena window_pages;
static unsigned int next_generation = 1;
static int next_window_id = 1;
static int is_dragging;
static Window* drag_win;
static int drag_offset_x, drag_offset_y;

static int pool_index(Window* w) {
    for (int i = 0; w && i < MAX_LAYERS; i++) if (&window_pool[i] == w) return i;
    return -1;
}

static unsigned int* page_ptr(int page) {
    return (unsigned int*)(MP_WINDOW_BUFFER_BASE + (unsigned int)page * WINDOW_PAGE_SIZE);
}

static void invalidate_bounds(const Window* w) {
    if (!w) return;
    if (w->borderless) video_invalidate_rect(w->x, w->y, w->w, w->h);
    else video_invalidate_rect(w->x - BORDER_WIDTH, w->y - BORDER_WIDTH,
                               w->w + BORDER_WIDTH * 2 + 3,
                               w->h + BORDER_WIDTH * 2 + 3);
}

static void finalize_locked(Window* w) {
    int i = pool_index(w);
    if (i < 0 || !window_used[i]) return;
    if (w->buffer_page_count > 0)
        slot_arena_free(&window_pages, w->buffer_page_first, w->buffer_page_count);
    window_used[i] = 0;
    window_titles[i][0] = 0;
    memset(w, 0, sizeof(*w));
}

static void release_locked(Window* w) {
    if (!w || w->ref_count <= 0) return;
    if (--w->ref_count == 0 && w->closing) finalize_locked(w);
}

static void destroy_locked(Window* w) {
    int index = -1;
    if (!w || w->closing) return;
    for (int i = 0; i < win_count; i++) if (layers[i] == w) { index = i; break; }
    if (index < 0) return;
    invalidate_bounds(w);
    for (int i = index; i < win_count - 1; i++) layers[i] = layers[i + 1];
    layers[--win_count] = 0;
    w->closing = 1;
    w->visible = 0;
    if (drag_win == w) { drag_win = 0; is_dragging = 0; }
    video_invalidate_rect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20);
    if (w->ref_count == 0) finalize_locked(w);
}

void win_init() {
    unsigned int flags = irq_save_disable();
    win_count = 0; is_dragging = 0; drag_win = 0;
    next_generation = 1; next_window_id = 1;
    slot_arena_init(&window_pages, WINDOW_ARENA_PAGES);
    for (int i = 0; i < MAX_LAYERS; i++) {
        layers[i] = 0; window_used[i] = 0; window_titles[i][0] = 0;
        memset(&window_pool[i], 0, sizeof(Window));
    }
    irq_restore(flags);
    video_request_redraw();
}

void win_bring_to_front(Window* w) {
    unsigned int flags = irq_save_disable();
    int index = -1;
    if (!w || w->closing || win_count <= 1 || layers[win_count - 1] == w) {
        irq_restore(flags); return;
    }
    for (int i = 0; i < win_count; i++) if (layers[i] == w) { index = i; break; }
    if (index >= 0) {
        Window* old_focus = layers[win_count - 1];
        for (int i = index; i < win_count - 1; i++) layers[i] = layers[i + 1];
        layers[win_count - 1] = w;
        invalidate_bounds(w);
        if (old_focus && !old_focus->borderless)
            video_invalidate_rect(old_focus->x, old_focus->y, old_focus->w, TITLE_BAR_HEIGHT);
    }
    irq_restore(flags);
}

void win_destroy(Window* w) {
    unsigned int flags = irq_save_disable();
    destroy_locked(w);
    irq_restore(flags);
}

Window* win_create(int x, int y, int w, int h, char* title, unsigned char color) {
    unsigned int flags, pixels;
    Window* result = 0;
    int pool_slot = -1, first, pages;

    if (w <= 0 || h <= 0 || w > SCREEN_WIDTH || h > SCREEN_HEIGHT) return 0;
    pixels = (unsigned int)w * (unsigned int)h;
    pages = (int)((pixels * sizeof(unsigned int) + WINDOW_PAGE_SIZE - 1) / WINDOW_PAGE_SIZE);
    flags = irq_save_disable();
    if (win_count >= MAX_LAYERS) { irq_restore(flags); klog_write("win layers full"); return 0; }
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (!window_used[i]) {
            pool_slot = i; window_used[i] = 1; result = &window_pool[i];
            memset(result, 0, sizeof(*result)); break;
        }
    }
    if (!result) { irq_restore(flags); return 0; }
    first = slot_arena_alloc(&window_pages, pages);
    if (first < 0) {
        window_used[pool_slot] = 0; irq_restore(flags); klog_write("win arena full"); return 0;
    }
    result->id = next_window_id++;
    result->generation = next_generation++;
    result->x = x; result->y = y; result->w = w; result->h = h;
    result->visible = 1; result->bg_color = color;
    result->buffer_slot = first;
    result->buffer_page_first = first; result->buffer_page_count = pages;
    result->buffer = page_ptr(first);
    if (!title) title = "tsk";
    {
        int i = 0;
        while (i < 31 && title[i]) { window_titles[pool_slot][i] = title[i]; i++; }
        window_titles[pool_slot][i] = 0;
    }
    result->title = window_titles[pool_slot];
    irq_restore(flags);

    /* The buffer is not published yet, so initialize it with IRQs enabled. */
    {
        unsigned int fill = video_color_to_rgb(color);
        for (unsigned int i = 0; i < pixels; i++) result->buffer[i] = fill;
    }

    flags = irq_save_disable();
    layers[win_count++] = result;
    irq_restore(flags);
    invalidate_bounds(result);
    video_invalidate_rect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20);
    return result;
}

void win_put_pixel(Window* w, int x, int y, unsigned int color) {
    if (w && w->buffer && !w->closing && x >= 0 && x < w->w && y >= 0 && y < w->h)
        w->buffer[y * w->w + x] = color & 0x00FFFFFFu;
}

unsigned int win_get_pixel(Window* w, int x, int y) {
    if (w && w->buffer && !w->closing && x >= 0 && x < w->w && y >= 0 && y < w->h)
        return w->buffer[y * w->w + x];
    return 0;
}

int win_snapshot_layers(Window** out, int max_count) {
    unsigned int flags;
    int count = 0;
    if (!out || max_count <= 0) return 0;
    flags = irq_save_disable();
    for (int i = 0; i < win_count && count < max_count; i++) {
        Window* w = layers[i];
        if (!w || w->closing || !w->visible) continue;
        w->ref_count++;
        out[count++] = w;
    }
    irq_restore(flags);
    return count;
}

void win_release_snapshot(Window** snapshot, int count) {
    unsigned int flags;
    if (!snapshot || count <= 0) return;
    flags = irq_save_disable();
    for (int i = 0; i < count; i++) release_locked(snapshot[i]);
    irq_restore(flags);
}

int win_set_title(Window* w, const char* title) {
    unsigned int flags;
    int i = 0;
    if (!w || !title) return 0;
    flags = irq_save_disable();
    if (w->closing || !w->title) { irq_restore(flags); return 0; }
    while (i < 31 && title[i]) { w->title[i] = title[i]; i++; }
    w->title[i] = 0;
    if (!w->borderless) video_invalidate_rect(w->x, w->y, w->w, TITLE_BAR_HEIGHT);
    irq_restore(flags);
    return 1;
}

void win_draw_all() {
    Window* snapshot[MAX_LAYERS];
    int count = win_snapshot_layers(snapshot, MAX_LAYERS);
    DamageRect clip;
    video_get_clip(&clip);

    for (int i = 0; i < count; i++) {
        Window* w = snapshot[i];
        if (!w->borderless) {
            draw_rect(w->x + 3, w->y + 3, w->w, w->h, C_DARK_GRAY);
            draw_rect(w->x - BORDER_WIDTH, w->y - BORDER_WIDTH,
                      w->w + BORDER_WIDTH * 2, w->h + BORDER_WIDTH * 2, C_WHITE);
        }
        if (w->buffer) {
            unsigned int* screen = (unsigned int*)MP_VIDEO_BACK_BUFFER_BASE;
            int x0 = w->x > clip.x ? w->x : clip.x;
            int y0 = w->y > clip.y ? w->y : clip.y;
            int x1 = w->x + w->w;
            int y1 = w->y + w->h;
            if (x1 > clip.x + clip.w) x1 = clip.x + clip.w;
            if (y1 > clip.y + clip.h) y1 = clip.y + clip.h;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
            if (y1 > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT;
            for (int y = y0; y < y1; y++) {
                unsigned int* src = w->buffer + (y - w->y) * w->w + x0 - w->x;
                unsigned int* dst = screen + y * SCREEN_WIDTH + x0;
                for (int x = x0; x < x1; x++) *dst++ = *src++ & 0x00FFFFFFu;
            }
        } else {
            draw_rect(w->x, w->y, w->w, w->h, w->bg_color);
        }

        if (!w->borderless) {
            int focused = i == count - 1;
            int by = w->y + (TITLE_BAR_HEIGHT - 11) / 2;
            draw_rect(w->x, w->y, w->w, TITLE_BAR_HEIGHT,
                      focused ? C_LIGHT_BLUE : C_BLUE);
            draw_string(w->x + 4, w->y + (TITLE_BAR_HEIGHT - 8) / 2,
                        w->title, C_WHITE);
            draw_rect(w->x + w->w - 14, by, 11, 11, C_WHITE);
            draw_rect(w->x + w->w - 13, by + 1, 9, 9, C_RED);
        }
        if (w->extra_draw) w->extra_draw(w);
    }
    win_release_snapshot(snapshot, count);
}

void win_handle_mouse(ps2_mouse_event_t* event, int mx, int my) {
    unsigned int flags;
    int left;
    if (!event) return;
    flags = irq_save_disable();
    left = event->buttons & 1;
    if (left) {
        if (!is_dragging) {
            for (int i = win_count - 1; i >= 0; i--) {
                Window* w = layers[i];
                if (!w || !w->visible || w->closing ||
                    mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) continue;
                if (!w->borderless) {
                    int bx = w->x + w->w - 14;
                    int by = w->y + (TITLE_BAR_HEIGHT - 11) / 2;
                    if (mx >= bx && mx < bx + 11 && my >= by && my < by + 11) {
                        Process* owner = process_find_by_window(w);
                        if (owner && owner->pid != 0) { klog_write_pair("window close ", owner->name); owner->state = PROCESS_DEAD; owner->win = 0; }
                        destroy_locked(w); irq_restore(flags); return;
                    }
                }
                irq_restore(flags); win_bring_to_front(w); flags = irq_save_disable();
                if (!w->borderless && my <= w->y + TITLE_BAR_HEIGHT) {
                    is_dragging = 1; drag_win = w;
                    drag_offset_x = mx - w->x; drag_offset_y = my - w->y;
                } else {
                    Process* owner = process_find_by_window(w);
                    if (owner && owner->pid != 0) {
                        owner->mouse_click_x = mx - w->x - (w->borderless ? 0 : BORDER_WIDTH);
                        owner->mouse_click_y = my - w->y - (w->borderless ? 0 : TITLE_BAR_HEIGHT);
                        owner->has_mouse_event = 1;
                    }
                }
                irq_restore(flags); return;
            }
        } else if (drag_win && !drag_win->closing) {
            int nx = mx - drag_offset_x, ny = my - drag_offset_y;
            if (drag_win->x != nx || drag_win->y != ny) {
                invalidate_bounds(drag_win); drag_win->x = nx; drag_win->y = ny; invalidate_bounds(drag_win);
            }
        }
    } else { is_dragging = 0; drag_win = 0; }
    irq_restore(flags);
}

int win_get_count() {
    unsigned int flags = irq_save_disable(); int count = win_count; irq_restore(flags); return count;
}
Window* win_get_at_layer(int index) {
    unsigned int flags = irq_save_disable();
    Window* w = index >= 0 && index < win_count ? layers[index] : 0;
    irq_restore(flags); return w;
}
Window* win_get_focused() {
    unsigned int flags = irq_save_disable();
    Window* w = win_count > 0 ? layers[win_count - 1] : 0;
    irq_restore(flags); return w;
}
