#include "ui.h"
#include "lib.h"
#include "theme.h"

static unsigned int ui_palette_rgb(int color) {
    static const unsigned int palette[16] = {
        TSUKI_RGB_INPUT, 0x6C8FF0u, TSUKI_RGB_GREEN, 0x74C7ECu,
        TSUKI_RGB_RED, 0xCBA6F7u, 0xFAB387u, 0xB8BBCBu,
        TSUKI_RGB_BORDER, TSUKI_RGB_ACCENT, TSUKI_RGB_GREEN, 0x89DCEBu,
        TSUKI_RGB_RED, 0xF5C2E7u, TSUKI_RGB_YELLOW, TSUKI_RGB_TEXT
    };
    return palette[color & 15];
}

static int ui_text_width(const char* text) {
    return text ? measure_text(text) : 0;
}

int ui_is_clicked(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

void ui_draw_button(int x, int y, int w, int h, const char* text, int bg, int fg, int pressed) {
    unsigned int fill = ui_palette_rgb(bg);
    unsigned int border = pressed ? TSUKI_RGB_ACCENT : TSUKI_RGB_BORDER;
    unsigned int text_rgb = ui_palette_rgb(fg);
    int radius = TSUKI_RADIUS;
    int tx, ty;

    if (w <= 0 || h <= 0) return;
    if (radius > h / 2) radius = h / 2;

    /* Flat surface: one-pixel outline, no Win9x-style highlight/shadow. */
    draw_round_rect_rgb(x, y, w, h, radius, border);
    if (w > 2 && h > 2) {
        draw_round_rect_rgb(x + 1, y + 1, w - 2, h - 2, radius - 1,
                            pressed ? TSUKI_RGB_CONTROL_HOVER : fill);
    }

    tx = x + (w - ui_text_width(text)) / 2;
    ty = y + (h - TSUKI_FONT_HEIGHT) / 2;
    if (text) draw_text_rgb(tx, ty, text, text_rgb);
}

void ui_draw_input(int x, int y, int w, int h, const char* text, int focused) {
    unsigned int border = focused ? TSUKI_RGB_ACCENT : TSUKI_RGB_BORDER;
    int text_width = ui_text_width(text);
    int radius = TSUKI_RADIUS;

    if (w <= 0 || h <= 0) return;
    if (radius > h / 2) radius = h / 2;

    draw_round_rect_rgb(x, y, w, h, radius, border);
    if (w > 2 && h > 2) {
        draw_round_rect_rgb(x + 1, y + 1, w - 2, h - 2, radius - 1, TSUKI_RGB_INPUT);
    }

    if (text) {
        draw_text_rgb(x + 8, y + (h - TSUKI_FONT_HEIGHT) / 2, text, TSUKI_RGB_TEXT);
    }

    if (focused) {
        int cursor_x = x + 8 + text_width;
        int cursor_h = TSUKI_FONT_HEIGHT + 2;
        int cursor_y = y + (h - cursor_h) / 2;
        if (cursor_x > x + w - 5) cursor_x = x + w - 5;
        draw_round_rect_rgb(cursor_x, cursor_y, 2, cursor_h, 1, TSUKI_RGB_ACCENT);
    }
}

void ui_draw_slider(int x, int y, int w, int h, int value, int max_value, int is_vertical) {
    if (w <= 0 || h <= 0) return;
    if (max_value <= 0) max_value = 1;
    if (value < 0) value = 0;
    if (value > max_value) value = max_value;

    if (is_vertical) {
        int track_w = w >= 6 ? 4 : 2;
        int track_x = x + (w - track_w) / 2;
        int thumb_h = h < 24 ? h : 24;
        int travel = h - thumb_h;
        int thumb_y = y + (value * travel) / max_value;

        draw_round_rect_rgb(track_x, y, track_w, h, track_w / 2, TSUKI_RGB_BORDER);
        draw_round_rect_rgb(x, thumb_y, w, thumb_h, TSUKI_RADIUS, TSUKI_RGB_ACCENT);
    } else {
        int track_h = h >= 6 ? 4 : 2;
        int track_y = y + (h - track_h) / 2;
        int thumb_w = w < 24 ? w : 24;
        int travel = w - thumb_w;
        int thumb_x = x + (value * travel) / max_value;

        draw_round_rect_rgb(x, track_y, w, track_h, track_h / 2, TSUKI_RGB_BORDER);
        draw_round_rect_rgb(thumb_x, y, thumb_w, h, TSUKI_RADIUS, TSUKI_RGB_ACCENT);
    }
}

void ui_draw_checkbox(int x, int y, int size, int checked, const char* label) {
    int radius;
    unsigned int border_color = checked ? TSUKI_RGB_ACCENT : TSUKI_RGB_BORDER;
    unsigned int fill_color = checked ? TSUKI_RGB_ACCENT : TSUKI_RGB_INPUT;

    if (size <= 0) size = 16;
    radius = size >= 8 ? 3 : 2;

    /* Box outline */
    draw_round_rect_rgb(x, y, size, size, radius, border_color);
    if (size > 2) {
        draw_round_rect_rgb(x + 1, y + 1, size - 2, size - 2, radius - 1, fill_color);
    }

    /* Checkmark: two short line segments */
    if (checked) {
        int cx = x + size / 2;
        int cy = y + size / 2;
        int half = size / 4;
        /* Left stroke: from (cx-half, cy) to (cx, cy+half) */
        for (int i = 0; i <= half; i++) {
            draw_rect_rgb(cx - half + i, cy + i - 1, 1, 2, TSUKI_RGB_TEXT);
        }
        /* Right stroke: from (cx, cy+half) to (cx+half, cy-half) */
        for (int i = 0; i <= half; i++) {
            draw_rect_rgb(cx + i, cy + half - i - 1, 1, 2, TSUKI_RGB_TEXT);
        }
    }

    /* Label text to the right */
    if (label) {
        draw_text_rgb(x + size + 6, y + (size - TSUKI_FONT_HEIGHT) / 2, label, TSUKI_RGB_TEXT);
    }
}

void ui_draw_label(int x, int y, const char* text, unsigned int bg_rgb, unsigned int fg_rgb) {
    int text_w;
    int padding = 8;
    int h = TSUKI_FONT_HEIGHT + 8;
    int radius = TSUKI_RADIUS;
    int w;

    if (!text) return;
    text_w = ui_text_width(text);
    w = text_w + padding * 2;
    if (radius > h / 2) radius = h / 2;

    draw_round_rect_rgb(x, y, w, h, radius, bg_rgb);
    draw_text_rgb(x + padding, y + (h - TSUKI_FONT_HEIGHT) / 2, text, fg_rgb);
}

void ui_draw_separator(int x, int y, int w) {
    if (w <= 0) return;
    draw_rect_rgb(x, y, w, 1, TSUKI_RGB_BORDER);
}
