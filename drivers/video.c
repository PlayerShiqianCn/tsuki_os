#include "video.h"
#include "mp.h"
#include "utils.h"
#include "disk.h"
#include "font8x8_basic.h"

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF
#define VBE_DISPI_INDEX_ID      0x0
#define VBE_DISPI_INDEX_XRES    0x1
#define VBE_DISPI_INDEX_YRES    0x2
#define VBE_DISPI_INDEX_BPP     0x3
#define VBE_DISPI_INDEX_ENABLE  0x4
#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40
#define VBE_DISPI_ID0           0xB0C0
#define VBE_DISPI_ID5           0xB0C5

static const unsigned int legacy_palette_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static const unsigned char cube_palette_8[6] = {0, 95, 135, 175, 215, 255};
static int video_vbe_ready = 0;
static unsigned int video_lfb_base = VBE_LFB_ADDRESS;
static int video_fb_width = FRAMEBUFFER_WIDTH;
static int video_fb_height = FRAMEBUFFER_HEIGHT;
static int video_redraw_pending = 1;
static int video_last_content_x = -1;
static int video_last_content_y = -1;
static int video_last_content_w = -1;
static int video_last_content_h = -1;

static unsigned int* video_back_buffer(void) {
    return (unsigned int*)MP_VIDEO_BACK_BUFFER_BASE;
}

static volatile unsigned int* video_framebuffer(void) {
    return (volatile unsigned int*)video_lfb_base;
}

static inline unsigned short inw_local(unsigned short port) {
    unsigned short ret;
    __asm__ __volatile__ ("inw %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outw_local(unsigned short port, unsigned short val) {
    __asm__ __volatile__ ("outw %0, %1" : : "a" (val), "Nd" (port));
}

static inline unsigned int inl_local(unsigned short port) {
    unsigned int ret;
    __asm__ __volatile__ ("inl %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outl_local(unsigned short port, unsigned int val) {
    __asm__ __volatile__ ("outl %0, %1" : : "a" (val), "Nd" (port));
}

static void bga_write(unsigned short index, unsigned short value) {
    outw_local(VBE_DISPI_IOPORT_INDEX, index);
    outw_local(VBE_DISPI_IOPORT_DATA, value);
}

static unsigned short bga_read(unsigned short index) {
    outw_local(VBE_DISPI_IOPORT_INDEX, index);
    return inw_local(VBE_DISPI_IOPORT_DATA);
}

static unsigned int pci_config_read32(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int address = 0x80000000u |
                           ((unsigned int)bus << 16) |
                           ((unsigned int)slot << 11) |
                           ((unsigned int)func << 8) |
                           (offset & 0xFC);
    outl_local(0xCF8, address);
    return inl_local(0xCFC);
}

static void pci_config_write32(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int value) {
    unsigned int address = 0x80000000u |
                           ((unsigned int)bus << 16) |
                           ((unsigned int)slot << 11) |
                           ((unsigned int)func << 8) |
                           (offset & 0xFC);
    outl_local(0xCF8, address);
    outl_local(0xCFC, value);
}

static unsigned int setup_vga_lfb_base(unsigned int desired_base) {
    for (int slot = 0; slot < 32; slot++) {
        for (int func = 0; func < 8; func++) {
            unsigned int vendor_device = pci_config_read32(0, (unsigned char)slot, (unsigned char)func, 0x00);
            if (vendor_device == 0xFFFFFFFFu) continue;

            unsigned int class_reg = pci_config_read32(0, (unsigned char)slot, (unsigned char)func, 0x08);
            unsigned char class_code = (unsigned char)(class_reg >> 24);
            unsigned char subclass = (unsigned char)(class_reg >> 16);
            if (class_code != 0x03 || (subclass != 0x00 && subclass != 0x80)) continue;

            unsigned int bar0 = pci_config_read32(0, (unsigned char)slot, (unsigned char)func, 0x10);
            unsigned int command;
            if ((bar0 & 0x1) != 0) continue;

            bar0 &= 0xFFFFFFF0u;
            if (bar0 == 0 || bar0 == 0xFFFFFFF0u) {
                pci_config_write32(0, (unsigned char)slot, (unsigned char)func, 0x10, desired_base & 0xFFFFFFF0u);
                bar0 = pci_config_read32(0, (unsigned char)slot, (unsigned char)func, 0x10) & 0xFFFFFFF0u;
            }

            command = pci_config_read32(0, (unsigned char)slot, (unsigned char)func, 0x04);
            if ((command & 0x2u) == 0) {
                pci_config_write32(0, (unsigned char)slot, (unsigned char)func, 0x04, command | 0x2u);
            }

            if (bar0 == 0 || bar0 == 0xFFFFFFF0u) continue;
            return bar0;
        }
    }
    return 0;
}

static void vga_set_palette_entry(unsigned char idx, unsigned char r, unsigned char g, unsigned char b) {
    outb(0x3C8, idx);
    outb(0x3C9, (unsigned char)(r >> 2));
    outb(0x3C9, (unsigned char)(g >> 2));
    outb(0x3C9, (unsigned char)(b >> 2));
}

static int quantize_channel_to_cube(unsigned char c) {
    return (c * 5 + 127) / 255;
}

static void init_vga_palette(void) {
    for (int i = 0; i < 16; i++) {
        unsigned int rgb = legacy_palette_rgb[i];
        vga_set_palette_entry((unsigned char)i,
                              (unsigned char)((rgb >> 16) & 0xFF),
                              (unsigned char)((rgb >> 8) & 0xFF),
                              (unsigned char)(rgb & 0xFF));
    }

    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int idx = 16 + 36 * r + 6 * g + b;
                vga_set_palette_entry((unsigned char)idx,
                                      cube_palette_8[r],
                                      cube_palette_8[g],
                                      cube_palette_8[b]);
            }
        }
    }

    for (int i = 0; i < 24; i++) {
        int value8 = 8 + i * 10;
        vga_set_palette_entry((unsigned char)(232 + i),
                              (unsigned char)value8,
                              (unsigned char)value8,
                              (unsigned char)value8);
    }
}

static int video_resolution_supported(int w, int h) {
    if (w == 640 && h == 480) return 1;
    if (w == 800 && h == 600) return 1;
    if (w == 1024 && h == 768) return 1;
    return 0;
}

static void video_invalidate_layout(void) {
    video_last_content_x = -1;
    video_last_content_y = -1;
    video_last_content_w = -1;
    video_last_content_h = -1;
}

static int try_enable_vbe_lfb(int width, int height) {
    unsigned short id = bga_read(VBE_DISPI_INDEX_ID);
    unsigned int lfb = setup_vga_lfb_base(VBE_LFB_ADDRESS);
    if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5) {
        return 0;
    }
    if (!lfb) {
        return 0;
    }

    video_lfb_base = lfb;

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, (unsigned short)width);
    bga_write(VBE_DISPI_INDEX_YRES, (unsigned short)height);
    bga_write(VBE_DISPI_INDEX_BPP, 32);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    return 1;
}

unsigned int video_color_to_rgb(unsigned char color) {
    return legacy_palette_rgb[color & 0x0F];
}

void video_init() {
    unsigned int fill = video_color_to_rgb(C_CYAN);
    unsigned int* back = video_back_buffer();

    video_vbe_ready = try_enable_vbe_lfb(video_fb_width, video_fb_height);
    if (!video_vbe_ready) {
        init_vga_palette();
    }

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        back[i] = fill;
    }

    if (video_vbe_ready) {
        volatile unsigned int* fb = video_framebuffer();
        for (int i = 0; i < video_fb_width * video_fb_height; i++) {
            fb[i] = 0x00000000u;
        }
    }

    video_invalidate_layout();
    video_redraw_pending = 1;
}

int video_set_resolution(int w, int h) {
    if (!video_resolution_supported(w, h)) return 0;

    video_fb_width = w;
    video_fb_height = h;

    if (!try_enable_vbe_lfb(video_fb_width, video_fb_height)) {
        video_vbe_ready = 0;
        return 0;
    }

    video_vbe_ready = 1;
    {
        volatile unsigned int* fb = video_framebuffer();
        for (int i = 0; i < video_fb_width * video_fb_height; i++) {
            fb[i] = 0x00000000u;
        }
    }
    video_invalidate_layout();
    video_redraw_pending = 1;
    return 1;
}

void video_get_resolution(int* w, int* h) {
    if (w) *w = video_fb_width;
    if (h) *h = video_fb_height;
}

void video_request_redraw(void) {
    video_redraw_pending = 1;
}

int video_consume_redraw(void) {
    int pending = video_redraw_pending;
    video_redraw_pending = 0;
    return pending;
}

void put_pixel_rgb(int x, int y, unsigned int rgb) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        video_back_buffer()[y * SCREEN_WIDTH + x] = rgb & 0x00FFFFFFu;
    }
}

void put_pixel(int x, int y, unsigned char color) {
    put_pixel_rgb(x, y, video_color_to_rgb(color));
}

void draw_rect_rgb(int x, int y, int w, int h, unsigned int rgb) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            put_pixel_rgb(x + j, y + i, rgb);
        }
    }
}

void draw_rect(int x, int y, int w, int h, unsigned char color) {
    draw_rect_rgb(x, y, w, h, video_color_to_rgb(color));
}

unsigned char video_rgb_to_index(unsigned int rgb) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8) & 0xFF;
    int b = rgb & 0xFF;

    int rc = quantize_channel_to_cube((unsigned char)r);
    int gc = quantize_channel_to_cube((unsigned char)g);
    int bc = quantize_channel_to_cube((unsigned char)b);
    int cube_idx = 16 + 36 * rc + 6 * gc + bc;

    int cube_r = cube_palette_8[rc];
    int cube_g = cube_palette_8[gc];
    int cube_b = cube_palette_8[bc];
    int cube_err = (r - cube_r) * (r - cube_r) +
                   (g - cube_g) * (g - cube_g) +
                   (b - cube_b) * (b - cube_b);

    int gray = (r * 30 + g * 59 + b * 11) / 100;
    int gray_step = (gray - 8 + 5) / 10;
    if (gray_step < 0) gray_step = 0;
    if (gray_step > 23) gray_step = 23;
    int gray_value = 8 + gray_step * 10;
    int gray_err = (r - gray_value) * (r - gray_value) +
                   (g - gray_value) * (g - gray_value) +
                   (b - gray_value) * (b - gray_value);

    if (gray_err < cube_err) {
        return (unsigned char)(232 + gray_step);
    }
    return (unsigned char)cube_idx;
}

static int get_font_index(char c) {
    unsigned char uc = (unsigned char)c;
    if (uc < 128) return (int)uc;
    return 0;
}

void draw_char(int x, int y, char c, unsigned char color) {
    unsigned int rgb = video_color_to_rgb(color);
    int font_idx = get_font_index(c);
    for (int i = 0; i < 8; i++) {
        unsigned char row = (unsigned char)font8x8_basic[font_idx][i];
        for (int j = 0; j < 8; j++) {
            if (((row >> j) & 1) != 0) {
                put_pixel_rgb(x + j, y + i, rgb);
            }
        }
    }
}

void draw_string(int x, int y, char* str, unsigned char color) {
    int i = 0;
    while (str[i] != 0) {
        draw_char(x + (i * 8), y, str[i], color);
        i++;
    }
}

void video_swap_buffer() {
    unsigned int* back = video_back_buffer();

    if (!video_vbe_ready) {
        unsigned char* vga = (unsigned char*)VGA_ADDRESS;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            vga[i] = video_rgb_to_index(back[i]);
        }
        return;
    }

    {
        volatile unsigned int* fb = video_framebuffer();
        int content_w = video_fb_width;
        int content_h = (video_fb_width * SCREEN_HEIGHT) / SCREEN_WIDTH;
        int offset_x;
        int offset_y;
        int layout_changed;

        if (content_h > video_fb_height) {
            content_h = video_fb_height;
            content_w = (video_fb_height * SCREEN_WIDTH) / SCREEN_HEIGHT;
        }
        if (content_w < 1) content_w = 1;
        if (content_h < 1) content_h = 1;

        offset_x = (video_fb_width - content_w) / 2;
        offset_y = (video_fb_height - content_h) / 2;

        layout_changed = (offset_x != video_last_content_x ||
                          offset_y != video_last_content_y ||
                          content_w != video_last_content_w ||
                          content_h != video_last_content_h);

        if (layout_changed) {
            for (int i = 0; i < video_fb_width * video_fb_height; i++) {
                fb[i] = 0x00000000u;
            }
            video_last_content_x = offset_x;
            video_last_content_y = offset_y;
            video_last_content_w = content_w;
            video_last_content_h = content_h;
        }

        for (int y = 0; y < content_h; y++) {
            int src_y = (y * SCREEN_HEIGHT) / content_h;
            volatile unsigned int* dst = fb + (offset_y + y) * video_fb_width + offset_x;

            for (int x = 0; x < content_w; x++) {
                int src_x = (x * SCREEN_WIDTH) / content_w;
                dst[x] = back[src_y * SCREEN_WIDTH + src_x];
            }
        }
    }
}

void draw_pixel(int x, int y, unsigned char color) {
    put_pixel(x, y, color);
}
