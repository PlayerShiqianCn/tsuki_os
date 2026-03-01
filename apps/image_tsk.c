#include "lib.h"
#include "jpeg.h"
#include "ui.h"

#define VIEW_W 296
#define VIEW_H 158
#define SIDEBAR_W 108
#define BUTTON_X 6
#define SIDEBAR_LABEL_Y 8
#define BUTTON_Y 22
#define BUTTON_W 96
#define BUTTON_H 18
#define BUTTON_GAP 4
#define PREVIEW_X 118
#define PREVIEW_Y 8
#define PREVIEW_W 172
#define PREVIEW_H 110
#define STATUS_Y 142
#define MAX_IMAGE_FILES 8
#define MAX_NAME 32
#define JR32_HEADER_SIZE 12
#define MAX_JPEG_W 160
#define MAX_JPEG_H 120
#define RGB24_IMAGE_SIZE (MAX_JPEG_W * MAX_JPEG_H * 3)
#define JR32_MAX_FILE (JR32_HEADER_SIZE + MAX_JPEG_W * MAX_JPEG_H * 4)

#define C_BLACK 0
#define C_BLUE 1
#define C_GREEN 2
#define C_CYAN 3
#define C_RED 4
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

static char image_files[MAX_IMAGE_FILES][MAX_NAME];
static int image_file_count = 0;
static int selected_index = -1;
static int is_focused = 0;
static int image_loaded = 0;
static int image_w = 0;
static int image_h = 0;
static int image_pixel_stride = 0;
static char status_line[48];
static char current_name[MAX_NAME];
static unsigned char image_file_buf[JR32_MAX_FILE];
static unsigned char* image_pixels = 0;

void main();

static int s_len(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int s_cmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        i++;
    }
    return (int)((unsigned char)a[i] - (unsigned char)b[i]);
}

static int s_ends_with(const char* s, const char* suffix) {
    int ls = s_len(s);
    int lf = s_len(suffix);
    if (ls < lf) return 0;
    return s_cmp(s + (ls - lf), suffix) == 0;
}

static void s_copy(char* dst, const char* src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void s_append(char* dst, const char* src, int max) {
    int base = s_len(dst);
    int i = 0;
    if (!dst || !src || max <= 0 || base >= max - 1) return;
    while (src[i] && base + i < max - 1) {
        dst[base + i] = src[i];
        i++;
    }
    dst[base + i] = '\0';
}

static void set_status(const char* text) {
    s_copy(status_line, text, sizeof(status_line));
}

static void build_jr32_path(const char* jpg_name, char* out, int max) {
    int len;
    out[0] = '\0';
    if (!jpg_name) return;

    s_copy(out, "image/", max);
    len = s_len(out);

    for (int i = 0; jpg_name[i] && len < max - 1; i++) {
        if (jpg_name[i] == '.' &&
            jpg_name[i + 1] == 'j' &&
            jpg_name[i + 2] == 'p' &&
            jpg_name[i + 3] == 'g' &&
            jpg_name[i + 4] == '\0') {
            break;
        }
        out[len++] = jpg_name[i];
    }
    out[len] = '\0';
    s_append(out, ".jr32._hid_", max);
}

static unsigned short read_u16_le(const unsigned char* p) {
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static int decode_jr32(const unsigned char* data, int size) {
    int expected;

    if (!data || size < JR32_HEADER_SIZE) return 0;
    if (data[0] != 'J' || data[1] != 'R' || data[2] != '3' || data[3] != '2') return 0;
    if (data[4] != 1) return 0;

    image_w = (int)read_u16_le(data + 5);
    image_h = (int)read_u16_le(data + 7);

    if (image_w <= 0 || image_h <= 0 || image_w > MAX_JPEG_W || image_h > MAX_JPEG_H) return 0;
    expected = image_w * image_h * 4;
    if (size != JR32_HEADER_SIZE + expected) return 0;

    image_pixels = (unsigned char*)(data + JR32_HEADER_SIZE);
    image_loaded = 1;
    image_pixel_stride = 4;
    return 1;
}

static void load_selected_image(void) {
    JpegInfo info;
    char path[48];
    int read;
    int jpg_size;

    image_loaded = 0;
    image_pixels = 0;
    image_pixel_stride = 0;

    if (selected_index < 0 || selected_index >= image_file_count) {
        set_status("No image selected.");
        return;
    }

    s_copy(path, "image/", sizeof(path));
    s_append(path, image_files[selected_index], sizeof(path));
    jpg_size = read_file(path, image_file_buf);
    if (jpg_size <= 0) {
        set_status("JPG read failed.");
        return;
    }
    if (!jpeg_probe(image_file_buf, jpg_size, &info)) {
        set_status("Bad JPG header.");
        return;
    }

    {
        int rgb_bytes = info.width * info.height * 3;
        if (rgb_bytes > 0 && rgb_bytes <= RGB24_IMAGE_SIZE &&
            jpg_size + rgb_bytes <= JR32_MAX_FILE &&
            jpeg_decode_rgb(image_file_buf, jpg_size, image_file_buf + jpg_size, JR32_MAX_FILE - jpg_size, &info)) {
            image_w = info.width;
            image_h = info.height;
            image_pixels = image_file_buf + jpg_size;
            image_loaded = 1;
            image_pixel_stride = 3;
            s_copy(current_name, image_files[selected_index], sizeof(current_name));
            if (info.progressive) {
                set_status("JPEG decoded (SOF2).");
            } else {
                set_status("JPEG decoded (SOF0).");
            }
            return;
        }
    }

    if (!info.progressive) {
        set_status("SOF0 decode failed.");
        return;
    }

    build_jr32_path(image_files[selected_index], path, sizeof(path));
    read = read_file(path, image_file_buf);
    if (read <= 0 || read > JR32_MAX_FILE) {
        set_status("Read failed.");
        return;
    }

    if (!decode_jr32(image_file_buf, read)) {
        set_status("JPEG decode failed.");
        return;
    }

    s_copy(current_name, image_files[selected_index], sizeof(current_name));
    set_status("JPEG loaded (SOF2 fallback).");
}

static void refresh_image_list(void) {
    char list_buf[512];
    int i = 0;

    image_file_count = 0;
    selected_index = -1;
    current_name[0] = '\0';
    image_loaded = 0;
    image_pixels = 0;

    if (!list_files_at(list_buf, sizeof(list_buf), "image")) {
        set_status("image/ missing.");
        return;
    }

    while (list_buf[i] && image_file_count < MAX_IMAGE_FILES) {
        char name[MAX_NAME];
        int p = 0;

        while (list_buf[i] && list_buf[i] != '\n') {
            if (p < MAX_NAME - 1) name[p++] = list_buf[i];
            i++;
        }
        if (list_buf[i] == '\n') i++;
        name[p] = '\0';

        if (name[0] == '\0') continue;
        if (!s_ends_with(name, ".jpg")) continue;

        s_copy(image_files[image_file_count], name, MAX_NAME);
        image_file_count++;
    }

    if (image_file_count == 0) {
        set_status("No JPG files.");
        return;
    }

    selected_index = 0;
    load_selected_image();
}

static void draw_preview(void) {
    draw_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, C_LIGHT_GRAY);
    draw_rect(PREVIEW_X + 1, PREVIEW_Y + 1, PREVIEW_W - 2, PREVIEW_H - 2, C_WHITE);

    if (!image_loaded || !image_pixels || image_w <= 0 || image_h <= 0) {
        draw_text(PREVIEW_X + 12, PREVIEW_Y + 48, "No preview", C_DARK_GRAY);
        return;
    }

    {
        int scale_x = (PREVIEW_W - 8) / image_w;
        int scale_y = (PREVIEW_H - 8) / image_h;
        int scale = scale_x < scale_y ? scale_x : scale_y;
        int draw_w;
        int draw_h;
        int start_x;
        int start_y;
        int src = 0;

        if (scale < 1) scale = 1;
        draw_w = image_w * scale;
        draw_h = image_h * scale;
        start_x = PREVIEW_X + (PREVIEW_W - draw_w) / 2;
        start_y = PREVIEW_Y + (PREVIEW_H - draw_h) / 2;

        for (int y = 0; y < image_h; y++) {
            for (int x = 0; x < image_w; x++) {
                unsigned int rgb = ((unsigned int)image_pixels[src] << 16) |
                                   ((unsigned int)image_pixels[src + 1] << 8) |
                                   (unsigned int)image_pixels[src + 2];
                draw_rect_rgb(start_x + x * scale, start_y + y * scale, scale, scale, rgb);
                src += image_pixel_stride;
            }
        }
    }
}

static void draw_focus_line(void) {
    draw_rect(0, 0, VIEW_W, 1, is_focused ? C_LIGHT_BLUE : C_DARK_GRAY);
}

static void render(void) {
    draw_rect(0, 0, VIEW_W, VIEW_H, C_LIGHT_GRAY);
    draw_focus_line();

    draw_rect(0, 1, SIDEBAR_W, VIEW_H - 1, C_BLUE);
    draw_text(8, SIDEBAR_LABEL_Y, "image/", C_WHITE);

    for (int i = 0; i < image_file_count; i++) {
        int y = BUTTON_Y + i * (BUTTON_H + BUTTON_GAP);
        int pressed = (i == selected_index);
        ui_draw_button(BUTTON_X, y, BUTTON_W, BUTTON_H, image_files[i], C_LIGHT_GRAY, C_BLACK, pressed);
    }

    draw_preview();
    draw_text(PREVIEW_X, PREVIEW_Y + PREVIEW_H + 6, current_name[0] ? current_name : "(none)", C_BLACK);
    draw_text(6, STATUS_Y, status_line[0] ? status_line : "Ready.", C_WHITE);
}

static void handle_click(int mx, int my) {
    for (int i = 0; i < image_file_count; i++) {
        int y = BUTTON_Y + i * (BUTTON_H + BUTTON_GAP);
        if (ui_is_clicked(mx, my, BUTTON_X, y, BUTTON_W, BUTTON_H)) {
            if (selected_index != i) {
                selected_index = i;
                load_selected_image();
            }
            render();
            return;
        }
    }
}

void main() {
    set_sandbox(1);
    win_set_title("JPEG Viewer");

    current_name[0] = '\0';
    status_line[0] = '\0';
    is_focused = win_is_focused();
    refresh_image_list();
    render();

    while (1) {
        int mx;
        int my;
        int ev;

        if (get_mouse_click(&mx, &my)) {
            handle_click(mx, my);
        }

        ev = win_get_event();
        if (ev & WIN_EVENT_FOCUS_CHANGED) {
            is_focused = win_is_focused();
            draw_focus_line();
        }
    }
}
