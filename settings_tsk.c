#include "lib.h"
#include "ui.h"

#define VIEW_W 296
#define VIEW_H 158
#define BUTTON_X 176
#define BUTTON_W 112
#define BUTTON_H 18
#define ROW1_Y 8
#define ROW2_Y 32
#define ROW3_Y 56
#define NET_BTN_Y 80
#define STATUS_Y 146
#define FILE_BUF_SIZE 512

#define C_BLACK 0
#define C_BLUE 1
#define C_GREEN 2
#define C_CYAN 3
#define C_RED 4
#define C_BROWN 6
#define C_LIGHT_GRAY 7
#define C_DARK_GRAY 8
#define C_LIGHT_BLUE 9
#define C_LIGHT_GREEN 10
#define C_YELLOW 14
#define C_WHITE 15

unsigned char settings_app_stack[4096];

typedef struct {
    int start_page_enabled;
    int wallpaper_style;
    int res_index;
    unsigned char local_ip[4];
    unsigned char gateway_ip[4];
    unsigned char dns_ip[4];
} SettingsConfig;

static SettingsConfig cfg;
static int is_focused = 0;
static char status_line[64];

static const int res_widths[3] = {640, 800, 1024};
static const int res_heights[3] = {480, 600, 768};
static const unsigned char dns_presets[3][4] = {
    {10, 0, 2, 3},
    {1, 1, 1, 1},
    {8, 8, 8, 8}
};

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

static int s_ncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)(ac - bc);
        if (ac == 0) return 0;
    }
    return 0;
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

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void set_status(const char* text) {
    s_copy(status_line, text, sizeof(status_line));
}

static void write_uint_text(char* out, int max, unsigned int value) {
    char tmp[16];
    int len = 0;
    int pos = s_len(out);

    if (!out || max <= 0 || pos >= max - 1) return;

    if (value == 0) {
        out[pos] = '0';
        out[pos + 1] = '\0';
        return;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (len > 0 && pos < max - 1) {
        out[pos++] = tmp[--len];
    }
    out[pos] = '\0';
}

static void append_ipv4_text(char* out, int max, const unsigned char ip[4]) {
    for (int i = 0; i < 4; i++) {
        if (i) s_append(out, ".", max);
        write_uint_text(out, max, (unsigned int)ip[i]);
    }
}

static int parse_ipv4_text(const char* s, unsigned char out[4]) {
    int idx = 0;
    int pos = 0;

    if (!s || !out) return 0;

    while (idx < 4) {
        int value = 0;
        int digits = 0;

        if (!is_digit(s[pos])) return 0;
        while (is_digit(s[pos])) {
            value = value * 10 + (s[pos] - '0');
            if (value > 255) return 0;
            pos++;
            digits++;
        }
        if (digits <= 0) return 0;
        out[idx++] = (unsigned char)value;

        if (idx == 4) break;
        if (s[pos] != '.') return 0;
        pos++;
    }

    return s[pos] == '\0';
}

static void set_defaults(void) {
    cfg.start_page_enabled = 1;
    cfg.wallpaper_style = 0;
    cfg.res_index = 0;
    cfg.local_ip[0] = 10; cfg.local_ip[1] = 0; cfg.local_ip[2] = 2; cfg.local_ip[3] = 15;
    cfg.gateway_ip[0] = 10; cfg.gateway_ip[1] = 0; cfg.gateway_ip[2] = 2; cfg.gateway_ip[3] = 2;
    cfg.dns_ip[0] = 10; cfg.dns_ip[1] = 0; cfg.dns_ip[2] = 2; cfg.dns_ip[3] = 3;
}

static int find_res_index(int w, int h) {
    for (int i = 0; i < 3; i++) {
        if (res_widths[i] == w && res_heights[i] == h) return i;
    }
    return 0;
}

static void apply_runtime(void) {
    set_start_page_enabled(cfg.start_page_enabled);
    set_wallpaper_style(cfg.wallpaper_style);
    net_set_local_ip(cfg.local_ip[0], cfg.local_ip[1], cfg.local_ip[2], cfg.local_ip[3]);
    net_set_gateway(cfg.gateway_ip[0], cfg.gateway_ip[1], cfg.gateway_ip[2], cfg.gateway_ip[3]);
    net_set_dns(cfg.dns_ip[0], cfg.dns_ip[1], cfg.dns_ip[2], cfg.dns_ip[3]);
}

static int save_config(void) {
    char buf[FILE_BUF_SIZE];
    int screen_w = res_widths[cfg.res_index];
    int screen_h = res_heights[cfg.res_index];

    buf[0] = '\0';
    s_append(buf, "# key=value\n", sizeof(buf));
    s_append(buf, "wallpaper=", sizeof(buf));
    if (cfg.wallpaper_style == 1) {
        s_append(buf, "ocean\n", sizeof(buf));
    } else if (cfg.wallpaper_style == 2) {
        s_append(buf, "forest\n", sizeof(buf));
    } else {
        s_append(buf, "default\n", sizeof(buf));
    }

    s_append(buf, "start_page=", sizeof(buf));
    s_append(buf, cfg.start_page_enabled ? "enabled\n" : "disabled\n", sizeof(buf));

    s_append(buf, "screen_w=", sizeof(buf));
    write_uint_text(buf, sizeof(buf), (unsigned int)screen_w);
    s_append(buf, "\n", sizeof(buf));

    s_append(buf, "screen_h=", sizeof(buf));
    write_uint_text(buf, sizeof(buf), (unsigned int)screen_h);
    s_append(buf, "\n", sizeof(buf));

    s_append(buf, "local_ip=", sizeof(buf));
    append_ipv4_text(buf, sizeof(buf), cfg.local_ip);
    s_append(buf, "\n", sizeof(buf));

    s_append(buf, "gateway=", sizeof(buf));
    append_ipv4_text(buf, sizeof(buf), cfg.gateway_ip);
    s_append(buf, "\n", sizeof(buf));

    s_append(buf, "dns=", sizeof(buf));
    append_ipv4_text(buf, sizeof(buf), cfg.dns_ip);
    s_append(buf, "\n", sizeof(buf));

    return write_file("system/config.rtsk", buf, s_len(buf)) == s_len(buf);
}

static void load_config(void) {
    char buf[FILE_BUF_SIZE];
    int read;
    int pos = 0;
    int screen_w = res_widths[0];
    int screen_h = res_heights[0];

    set_defaults();
    read = read_file("system/config.rtsk", buf);
    if (read <= 0) {
        set_status("Config missing. Defaults used.");
        apply_runtime();
        return;
    }
    if (read >= FILE_BUF_SIZE) read = FILE_BUF_SIZE - 1;
    buf[read] = '\0';

    while (pos < read) {
        char line[96];
        int li = 0;

        while (pos < read && (buf[pos] == '\r' || buf[pos] == '\n')) pos++;
        if (pos >= read) break;

        while (pos < read && buf[pos] != '\r' && buf[pos] != '\n' && li < (int)sizeof(line) - 1) {
            line[li++] = buf[pos++];
        }
        while (pos < read && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        line[li] = '\0';

        if (!line[0] || line[0] == '#') continue;

        if (s_ncmp(line, "wallpaper=", 10) == 0) {
            const char* value = line + 10;
            if (s_cmp(value, "ocean") == 0) cfg.wallpaper_style = 1;
            else if (s_cmp(value, "forest") == 0) cfg.wallpaper_style = 2;
            else cfg.wallpaper_style = 0;
        } else if (s_ncmp(line, "start_page=", 11) == 0) {
            const char* value = line + 11;
            cfg.start_page_enabled = (s_cmp(value, "disabled") != 0 && s_cmp(value, "off") != 0);
        } else if (s_ncmp(line, "screen_w=", 9) == 0) {
            int n = 0;
            const char* value = line + 9;
            while (*value >= '0' && *value <= '9') {
                n = n * 10 + (*value - '0');
                value++;
            }
            if (n > 0) screen_w = n;
        } else if (s_ncmp(line, "screen_h=", 9) == 0) {
            int n = 0;
            const char* value = line + 9;
            while (*value >= '0' && *value <= '9') {
                n = n * 10 + (*value - '0');
                value++;
            }
            if (n > 0) screen_h = n;
        } else if (s_ncmp(line, "local_ip=", 9) == 0) {
            parse_ipv4_text(line + 9, cfg.local_ip);
        } else if (s_ncmp(line, "gateway=", 8) == 0) {
            parse_ipv4_text(line + 8, cfg.gateway_ip);
        } else if (s_ncmp(line, "dns=", 4) == 0) {
            parse_ipv4_text(line + 4, cfg.dns_ip);
        }
    }

    cfg.res_index = find_res_index(screen_w, screen_h);
    apply_runtime();
    set_status("Loaded system config.");
}

static void dns_cycle(void) {
    int next = 0;
    for (int i = 0; i < 3; i++) {
        if (cfg.dns_ip[0] == dns_presets[i][0] &&
            cfg.dns_ip[1] == dns_presets[i][1] &&
            cfg.dns_ip[2] == dns_presets[i][2] &&
            cfg.dns_ip[3] == dns_presets[i][3]) {
            next = (i + 1) % 3;
            break;
        }
    }

    cfg.dns_ip[0] = dns_presets[next][0];
    cfg.dns_ip[1] = dns_presets[next][1];
    cfg.dns_ip[2] = dns_presets[next][2];
    cfg.dns_ip[3] = dns_presets[next][3];
    apply_runtime();
    if (save_config()) {
        set_status("DNS updated.");
    } else {
        set_status("Save failed.");
    }
}

static void apply_qemu_nat(void) {
    cfg.local_ip[0] = 10; cfg.local_ip[1] = 0; cfg.local_ip[2] = 2; cfg.local_ip[3] = 15;
    cfg.gateway_ip[0] = 10; cfg.gateway_ip[1] = 0; cfg.gateway_ip[2] = 2; cfg.gateway_ip[3] = 2;
    cfg.dns_ip[0] = 10; cfg.dns_ip[1] = 0; cfg.dns_ip[2] = 2; cfg.dns_ip[3] = 3;
    apply_runtime();
    if (save_config()) {
        set_status("Applied QEMU NAT.");
    } else {
        set_status("Save failed.");
    }
}

static const char* wallpaper_text(void) {
    if (cfg.wallpaper_style == 1) return "Ocean";
    if (cfg.wallpaper_style == 2) return "Forest";
    return "Default";
}

static void build_resolution_text(char* out, int max) {
    if (!out || max <= 0) return;
    out[0] = '\0';
    write_uint_text(out, max, (unsigned int)res_widths[cfg.res_index]);
    s_append(out, "x", max);
    write_uint_text(out, max, (unsigned int)res_heights[cfg.res_index]);
}

static void build_dns_button_text(char* out, int max) {
    if (!out || max <= 0) return;
    out[0] = '\0';
    s_append(out, "DNS ", max);
    write_uint_text(out, max, (unsigned int)cfg.dns_ip[0]);
}

static void build_net_line(char* out, int max, const char* label, const unsigned char ip[4]) {
    if (!out || max <= 0) return;
    out[0] = '\0';
    s_append(out, label, max);
    s_append(out, " ", max);
    append_ipv4_text(out, max, ip);
}

static void render(void) {
    char res_text[24];
    char dns_button[24];
    char line[40];

    draw_rect(0, 0, VIEW_W, VIEW_H, C_LIGHT_GRAY);
    draw_rect(0, 0, VIEW_W, 1, is_focused ? C_LIGHT_BLUE : C_DARK_GRAY);

    draw_text(8, ROW1_Y + 5, "Start Page", C_BLACK);
    ui_draw_button(BUTTON_X, ROW1_Y, BUTTON_W, BUTTON_H,
                   cfg.start_page_enabled ? "Enabled" : "Disabled",
                   cfg.start_page_enabled ? C_GREEN : C_RED, C_WHITE, 0);

    draw_text(8, ROW2_Y + 5, "Wallpaper", C_BLACK);
    ui_draw_button(BUTTON_X, ROW2_Y, BUTTON_W, BUTTON_H,
                   wallpaper_text(), C_BLUE, C_WHITE, 0);

    build_resolution_text(res_text, sizeof(res_text));
    draw_text(8, ROW3_Y + 5, "Screen Mode", C_BLACK);
    ui_draw_button(BUTTON_X, ROW3_Y, BUTTON_W, BUTTON_H,
                   res_text, C_CYAN, C_BLACK, 0);

    draw_text(8, 72, "Screen mode switches live.", C_DARK_GRAY);

    ui_draw_button(8, NET_BTN_Y, 88, BUTTON_H, "QEMU NAT", C_GREEN, C_WHITE, 0);
    build_dns_button_text(dns_button, sizeof(dns_button));
    ui_draw_button(104, NET_BTN_Y, 88, BUTTON_H, dns_button, C_YELLOW, C_BLACK, 0);
    ui_draw_button(200, NET_BTN_Y, 88, BUTTON_H, "Reload", C_BLUE, C_WHITE, 0);

    build_net_line(line, sizeof(line), "IP", cfg.local_ip);
    draw_text(8, 108, line, C_BLACK);
    build_net_line(line, sizeof(line), "GW", cfg.gateway_ip);
    draw_text(8, 118, line, C_BLACK);
    build_net_line(line, sizeof(line), "DNS", cfg.dns_ip);
    draw_text(8, 128, line, C_BLACK);

    draw_text(8, 138, "Changes save to /system/config.rtsk", C_DARK_GRAY);
    draw_text(8, STATUS_Y, status_line[0] ? status_line : "Ready.", C_BLACK);
}

static void toggle_start_page(void) {
    cfg.start_page_enabled = !cfg.start_page_enabled;
    apply_runtime();
    if (save_config()) {
        set_status(cfg.start_page_enabled ? "Start page enabled." : "Start page disabled.");
    } else {
        set_status("Save failed.");
    }
}

static void cycle_wallpaper(void) {
    cfg.wallpaper_style = (cfg.wallpaper_style + 1) % 3;
    apply_runtime();
    if (save_config()) {
        set_status("Wallpaper updated.");
    } else {
        set_status("Save failed.");
    }
}

static void cycle_resolution(void) {
    cfg.res_index = (cfg.res_index + 1) % 3;
    if (save_config()) {
        set_status("Screen mode applied.");
    } else {
        set_status("Save failed.");
    }
}

static void handle_click(int mx, int my) {
    if (ui_is_clicked(mx, my, BUTTON_X, ROW1_Y, BUTTON_W, BUTTON_H)) {
        toggle_start_page();
        render();
        return;
    }

    if (ui_is_clicked(mx, my, BUTTON_X, ROW2_Y, BUTTON_W, BUTTON_H)) {
        cycle_wallpaper();
        render();
        return;
    }

    if (ui_is_clicked(mx, my, BUTTON_X, ROW3_Y, BUTTON_W, BUTTON_H)) {
        cycle_resolution();
        render();
        return;
    }

    if (ui_is_clicked(mx, my, 8, NET_BTN_Y, 88, BUTTON_H)) {
        apply_qemu_nat();
        render();
        return;
    }

    if (ui_is_clicked(mx, my, 104, NET_BTN_Y, 88, BUTTON_H)) {
        dns_cycle();
        render();
        return;
    }

    if (ui_is_clicked(mx, my, 200, NET_BTN_Y, 88, BUTTON_H)) {
        load_config();
        render();
    }
}

void main() {
    set_sandbox(0);
    win_set_title("Settings");

    status_line[0] = '\0';
    is_focused = win_is_focused();
    load_config();
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
            render();
        }

        if (ev & WIN_EVENT_KEY_READY) {
            int key = get_key();
            if (key == 'q' || key == 27) {
                exit();
            }
        }
    }
}
