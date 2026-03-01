__asm__(".code32");

#include "kernel_config.h"

#include "fs.h"
#include "net.h"
#include "utils.h"
#include "video.h"

static int desktop_wallpaper_style = 0;
static int start_page_enabled = 1;

void kernel_set_wallpaper_style(int style) {
    if (style < 0) style = 0;
    if (style > 2) style = 2;
    if (desktop_wallpaper_style == style) return;
    desktop_wallpaper_style = style;
    video_request_redraw();
}

int kernel_get_wallpaper_style(void) {
    return desktop_wallpaper_style;
}

void kernel_set_start_page_enabled(int enabled) {
    int value = enabled ? 1 : 0;

    if (start_page_enabled == value) return;
    start_page_enabled = value;
    video_request_redraw();
}

int kernel_is_start_page_enabled(void) {
    return start_page_enabled;
}

static int parse_ipv4_local(const char* s, unsigned char out[4]) {
    int idx = 0;
    int pos = 0;

    if (!s || !out) return 0;

    while (idx < 4) {
        int value = 0;
        int digits = 0;

        if (s[pos] < '0' || s[pos] > '9') return 0;
        while (s[pos] >= '0' && s[pos] <= '9') {
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

void kernel_reload_system_config(void) {
    SystemFile file;
    char buf[512];
    int n;
    int pos = 0;
    int screen_w = 0;
    int screen_h = 0;

    video_get_resolution(&screen_w, &screen_h);

    if (!sys_file_open("system/config.rtsk", &file)) return;
    if (file.size == 0 || file.size >= sizeof(buf)) return;

    n = fs_read_file("system/config.rtsk", buf);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    buf[n] = '\0';

    while (pos < n) {
        char line[96];
        int li = 0;

        while (pos < n && (buf[pos] == '\r' || buf[pos] == '\n')) pos++;
        if (pos >= n) break;

        while (pos < n && buf[pos] != '\r' && buf[pos] != '\n' && li < (int)sizeof(line) - 1) {
            line[li++] = buf[pos++];
        }
        while (pos < n && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        line[li] = '\0';

        if (!line[0] || line[0] == '#') continue;

        if (strncmp(line, "wallpaper=", 10) == 0) {
            const char* value = line + 10;

            if (strcmp(value, "ocean") == 0) {
                kernel_set_wallpaper_style(1);
            } else if (strcmp(value, "forest") == 0) {
                kernel_set_wallpaper_style(2);
            } else {
                kernel_set_wallpaper_style(0);
            }
        } else if (strncmp(line, "start_page=", 11) == 0) {
            const char* value = line + 11;

            kernel_set_start_page_enabled(strcmp(value, "disabled") != 0 && strcmp(value, "off") != 0);
        } else if (strncmp(line, "screen_w=", 9) == 0) {
            int value = 0;
            const char* p = line + 9;

            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                p++;
            }
            if (value > 0) screen_w = value;
        } else if (strncmp(line, "screen_h=", 9) == 0) {
            int value = 0;
            const char* p = line + 9;

            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                p++;
            }
            if (value > 0) screen_h = value;
        } else if (strncmp(line, "local_ip=", 9) == 0) {
            unsigned char ip[4];

            if (parse_ipv4_local(line + 9, ip)) {
                net_set_local_ip(ip[0], ip[1], ip[2], ip[3]);
            }
        } else if (strncmp(line, "gateway=", 8) == 0) {
            unsigned char ip[4];

            if (parse_ipv4_local(line + 8, ip)) {
                net_set_gateway(ip[0], ip[1], ip[2], ip[3]);
            }
        } else if (strncmp(line, "dns=", 4) == 0) {
            unsigned char ip[4];

            if (parse_ipv4_local(line + 4, ip)) {
                net_set_dns_server(ip[0], ip[1], ip[2], ip[3]);
            }
        }
    }

    video_set_resolution(screen_w, screen_h);
}
