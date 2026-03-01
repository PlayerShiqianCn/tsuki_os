#include "lib.h"
#include "ui.h"

#define TERM_CLIENT_W 236
#define TERM_CLIENT_H 148
#define TERM_PADDING 4
#define TERM_SCROLL_W 16
#define TERM_INPUT_X 2
#define TERM_INPUT_W (TERM_CLIENT_W - 4)
#define TERM_INPUT_H 18
#define TERM_INPUT_Y (TERM_CLIENT_H - TERM_INPUT_H - 2)
#define TERM_COLS ((TERM_CLIENT_W - TERM_SCROLL_W - TERM_PADDING - 4) / 8)
#define TERM_OUTPUT_ROWS ((TERM_INPUT_Y - TERM_PADDING) / 8)
#define INPUT_MAX 64
#define FILE_BUF_SIZE 2048
#define NET_HTTP_BUF_SIZE 1536
#define HISTORY_MAX 100
#define HIDDEN_SUFFIX "._hid_"

static char history[HISTORY_MAX][TERM_COLS + 1];
static int history_count = 0;
static int view_start_row = 0;

static int out_col = 0;
static char input_buf[INPUT_MAX];
static int input_len = 0;
static int is_focused = 0;
static int sudo_mode = 0;
static char current_dir[INPUT_MAX];
static char net_http_buf[NET_HTTP_BUF_SIZE];

void main();
static void push_char(char ch);
static void write_text(const char* s);
static void write_line(const char* s);
static void s_copy(char* dst, const char* src, int max);
static void s_append(char* dst, const char* src, int max);
static void print_system_version(void);

__attribute__((naked)) void _start() {
    __asm__ volatile (
        "call main \n\t"
        "call exit \n\t"
        :
        :
        : "memory"
    );
}

// 简单的字符串函数
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

static int s_ends_with(const char* s, const char* suffix) {
    int ls = s_len(s);
    int lf = s_len(suffix);
    if (ls < lf) return 0;
    return s_cmp(s + (ls - lf), suffix) == 0;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void write_uint(unsigned int value) {
    char tmp[16];
    int len = 0;

    if (value == 0) {
        push_char('0');
        return;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (len > 0) {
        push_char(tmp[--len]);
    }
}

static void write_hex_byte(unsigned char value) {
    static const char hex[] = "0123456789ABCDEF";
    push_char(hex[(value >> 4) & 0x0F]);
    push_char(hex[value & 0x0F]);
}

static void write_hex_word(unsigned short value) {
    write_hex_byte((unsigned char)((value >> 8) & 0xFF));
    write_hex_byte((unsigned char)(value & 0xFF));
}

static void write_ipv4_addr(const unsigned char ip[4]) {
    if (!ip) {
        write_text("0.0.0.0");
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (i) push_char('.');
        write_uint(ip[i]);
    }
}

static void write_mac_addr(const unsigned char mac[6]) {
    if (!mac) {
        write_text("00:00:00:00:00:00");
        return;
    }
    for (int i = 0; i < 6; i++) {
        if (i) push_char(':');
        write_hex_byte(mac[i]);
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

static char* find_http_body(char* resp) {
    int i;

    if (!resp) return 0;

    for (i = 0; resp[i]; i++) {
        if (resp[i] == '\r' && resp[i + 1] == '\n' &&
            resp[i + 2] == '\r' && resp[i + 3] == '\n') {
            return resp + i + 4;
        }
    }

    for (i = 0; resp[i]; i++) {
        if (resp[i] == '\n' && resp[i + 1] == '\n') {
            return resp + i + 2;
        }
    }

    return resp;
}

static void print_net_info(void) {
    NetInfo info;

    if (!net_get_info(&info)) {
        write_line("net info failed.");
        return;
    }

    if (!info.present) {
        write_line("No NIC detected.");
        return;
    }

    write_text("PCI ");
    write_hex_word(info.vendor_id);
    push_char(':');
    write_hex_word(info.device_id);
    push_char('\n');

    write_text("bus ");
    write_uint(info.bus);
    write_text(" slot ");
    write_uint(info.slot);
    write_text(" func ");
    write_uint(info.func);
    write_text(" irq ");
    write_uint(info.irq_line);
    push_char('\n');

    write_text("state ");
    if (info.initialized && info.tx_ready && info.rx_ready) {
        write_line("ready");
    } else if (info.initialized) {
        write_line("partial");
    } else {
        write_line("not ready");
    }

    write_text("mac ");
    write_mac_addr(info.mac);
    push_char('\n');

    write_text("ip  ");
    write_ipv4_addr(info.local_ip);
    push_char('\n');

    write_text("gw  ");
    write_ipv4_addr(info.gateway_ip);
    push_char('\n');

    write_text("dns ");
    write_ipv4_addr(info.dns_ip);
    push_char('\n');

    write_text("tx=");
    write_uint(info.tx_ok_count);
    write_text(" rx=");
    write_uint(info.rx_ok_count);
    write_text(" ping=");
    write_uint(info.ping_ok_count);
    push_char('\n');

    write_text("dns=");
    write_uint(info.dns_ok_count);
    write_text(" tcp=");
    write_uint(info.tcp_ok_count);
    write_text(" http=");
    write_uint(info.curl_ok_count);
    push_char('\n');
}

static void build_prompt(char* out, int max) {
    if (!out || max <= 0) return;

    out[0] = '\0';
    if (sudo_mode) {
        s_copy(out, "root ", max);
    }

    if (current_dir[0]) {
        s_append(out, "/", max);
        s_append(out, current_dir, max);
    } else {
        s_append(out, "/", max);
    }

    s_append(out, " > ", max);
}

static void s_copy(char* dst, const char* src, int max) {
    if (max <= 0) return;
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void s_append(char* dst, const char* src, int max) {
    int base = s_len(dst);
    int i = 0;
    if (base >= max - 1) return;
    while (src[i] && (base + i) < max - 1) {
        dst[base + i] = src[i];
        i++;
    }
    dst[base + i] = '\0';
}

static int is_hidden_name(const char* name) {
    return s_ends_with(name, HIDDEN_SUFFIX);
}

static int has_slash(const char* s) {
    for (int i = 0; s && s[i]; i++) {
        if (s[i] == '/') return 1;
    }
    return 0;
}

static void strip_leading_slash(const char* src, char* dst, int max) {
    while (src && *src == '/') src++;
    s_copy(dst, src ? src : "", max);
}

static void split_dir_and_name(const char* path, char* dir, int dir_max, char* name, int name_max) {
    int last_slash = -1;
    int i;

    if (!dir || !name || dir_max <= 0 || name_max <= 0) return;
    dir[0] = '\0';
    name[0] = '\0';
    if (!path || !path[0]) return;

    for (i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        s_copy(name, path, name_max);
        return;
    }

    if (last_slash >= dir_max) last_slash = dir_max - 1;
    for (i = 0; i < last_slash; i++) dir[i] = path[i];
    dir[last_slash] = '\0';
    s_copy(name, path + last_slash + 1, name_max);
}

static void resolve_input_path(const char* input, char* out, int out_max) {
    char trimmed[INPUT_MAX];

    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!input) return;

    strip_leading_slash(input, trimmed, sizeof(trimmed));

    if (trimmed[0] == '\0' || s_cmp(trimmed, ".") == 0) {
        s_copy(out, current_dir, out_max);
        return;
    }
    if (s_cmp(trimmed, "..") == 0) {
        out[0] = '\0';
        return;
    }

    if (current_dir[0] && !has_slash(trimmed)) {
        s_copy(out, current_dir, out_max);
        s_append(out, "/", out_max);
        s_append(out, trimmed, out_max);
        return;
    }

    s_copy(out, trimmed, out_max);
}

static int path_exists(const char* path) {
    char dir[INPUT_MAX];
    char name[INPUT_MAX];
    char list_buf[512];
    int i = 0;

    if (!path) return 0;
    if (path[0] == '\0') return 1;

    split_dir_and_name(path, dir, sizeof(dir), name, sizeof(name));
    if (name[0] == '\0') {
        return list_files_at(list_buf, sizeof(list_buf), dir[0] ? dir : 0);
    }

    if (!list_files_at(list_buf, sizeof(list_buf), dir[0] ? dir : 0)) return 0;

    while (list_buf[i]) {
        char item[96];
        int p = 0;

        while (list_buf[i] && list_buf[i] != '\n') {
            if (p < (int)sizeof(item) - 1) item[p++] = list_buf[i];
            i++;
        }
        if (list_buf[i] == '\n') i++;
        item[p] = '\0';

        if (s_cmp(item, name) == 0) return 1;
    }

    return 0;
}

static void print_cwd(void) {
    write_text("cwd: ");
    if (current_dir[0]) write_line(current_dir);
    else write_line("/");
}

static void format_visible_name(const char* real_name, int allow_hidden, char* out, int out_max) {
    out[0] = '\0';

    if (is_hidden_name(real_name)) {
        if (!allow_hidden) return;
        s_copy(out, real_name, out_max);
        return;
    }

    s_copy(out, real_name, out_max);
}

static int map_input_name_to_real(const char* input_name, int allow_hidden, char* out_real, int out_max) {
    char resolved[INPUT_MAX];

    if (!input_name || !out_real || out_max <= 0) return 0;

    resolve_input_path(input_name, resolved, sizeof(resolved));
    if (resolved[0] == '\0') return 0;
    s_copy(out_real, resolved, out_max);

    if (is_hidden_name(out_real) && !allow_hidden) {
        return 0;
    }

    if (!allow_hidden) {
        char hidden_candidate[INPUT_MAX];
        s_copy(hidden_candidate, out_real, sizeof(hidden_candidate));
        s_append(hidden_candidate, HIDDEN_SUFFIX, sizeof(hidden_candidate));
        if (path_exists(hidden_candidate)) return 0;
    } else if (!is_hidden_name(out_real) && !path_exists(out_real)) {
        char hidden_candidate[INPUT_MAX];
        s_copy(hidden_candidate, out_real, sizeof(hidden_candidate));
        s_append(hidden_candidate, HIDDEN_SUFFIX, sizeof(hidden_candidate));
        if (path_exists(hidden_candidate)) {
            s_copy(out_real, hidden_candidate, out_max);
        }
    }

    return 1;
}

static void clear_output(void) {
    history_count = 0;
    view_start_row = 0;
    out_col = 0;
    // 初始化第一行
    history[0][0] = '\0';
    history_count = 1;
}

static void push_char(char ch) {
    if (ch == '\r') return;
    
    int current_row = history_count - 1;
    
    if (ch == '\n') {
        out_col = 0;
        if (history_count < HISTORY_MAX) {
            history_count++;
            history[history_count - 1][0] = '\0';
        } else {
            // 历史记录满了，整体上移
            for (int i = 1; i < HISTORY_MAX; i++) {
                for (int j = 0; j <= TERM_COLS; j++) {
                    history[i - 1][j] = history[i][j];
                }
            }
            history[HISTORY_MAX - 1][0] = '\0';
        }
        
        // 如果原本在最底部，则自动滚动跟随
        int max_view = history_count - TERM_OUTPUT_ROWS;
        if (max_view < 0) max_view = 0;
        
        // 允许有一行误差的跟随
        if (view_start_row >= max_view - 1) {
            view_start_row = max_view;
        }
        return;
    }
    
    if (ch == '\t') {
        for (int i = 0; i < 4; i++) push_char(' ');
        return;
    }
    
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) {
        ch = '.';
    }
    
    if (out_col >= TERM_COLS) {
        push_char('\n');
        current_row = history_count - 1; // 重新获取行号
    }
    
    history[current_row][out_col++] = ch;
    history[current_row][out_col] = '\0';
}

static void write_text(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) push_char(s[i]);
}

static void write_line(const char* s) {
    write_text(s);
    push_char('\n');
}

static int read_version_field(const char* text, const char* key, char* out, int out_max) {
    int pos = 0;
    int key_len;

    if (!text || !key || !out || out_max <= 0) return 0;
    out[0] = '\0';
    key_len = s_len(key);
    if (key_len <= 0) return 0;

    while (text[pos]) {
        int line_start = pos;
        int out_i = 0;

        while (text[pos] && text[pos] != '\n' && text[pos] != '\r') pos++;
        if (s_ncmp(text + line_start, key, key_len) == 0 && text[line_start + key_len] == '=') {
            line_start += key_len + 1;
            while (text[line_start] && text[line_start] != '\n' && text[line_start] != '\r' &&
                   out_i < out_max - 1) {
                out[out_i++] = text[line_start++];
            }
            out[out_i] = '\0';
            return out_i > 0;
        }
        while (text[pos] == '\n' || text[pos] == '\r') pos++;
    }

    return 0;
}

static void print_system_version(void) {
    char file_buf[FILE_BUF_SIZE];
    char version[48];
    char build[24];
    int n = read_file("system/version.txt", file_buf);
    int has_version;
    int has_build;

    if (n <= 0) {
        write_line("Version read failed.");
        return;
    }
    if (n >= FILE_BUF_SIZE) n = FILE_BUF_SIZE - 1;
    file_buf[n] = '\0';

    has_version = read_version_field(file_buf, "version", version, sizeof(version));
    has_build = read_version_field(file_buf, "build", build, sizeof(build));

    if (has_version) {
        write_text("Version ");
        write_text(version);
        if (has_build) {
            write_text(" (build ");
            write_text(build);
            write_line(")");
        } else {
            push_char('\n');
        }
        return;
    }

    write_text("Version ");
    write_text(file_buf);
    if (file_buf[n - 1] != '\n') push_char('\n');
}

static void render(void) {
    int accent = is_focused ? 9 : 8;

    // 使用真实客户区尺寸，避免右侧/底部被窗口裁掉
    draw_rect(0, 0, TERM_CLIENT_W, TERM_CLIENT_H, 0);
    draw_rect(0, 0, TERM_CLIENT_W, 1, accent);

    // 绘制历史记录文本
    for (int r = 0; r < TERM_OUTPUT_ROWS; r++) {
        int history_idx = view_start_row + r;
        if (history_idx < history_count) {
            draw_text(TERM_PADDING, TERM_PADDING + r * 8, history[history_idx], 10);
        }
    }

    // 绘制滑块 (UI库)
    int slider_x = TERM_CLIENT_W - TERM_SCROLL_W;
    int slider_y = 2;
    int slider_w = TERM_SCROLL_W;
    int slider_h = TERM_INPUT_Y - slider_y - 2;
    
    int max_view = history_count - TERM_OUTPUT_ROWS;
    if (max_view < 0) max_view = 0;
    
    ui_draw_slider(slider_x, slider_y, slider_w, slider_h, view_start_row, max_view, 1);
    draw_rect(0, TERM_INPUT_Y - 2, TERM_CLIENT_W, 1, accent);

    // 绘制输入框 (UI库)
    char prompt[INPUT_MAX + 8];
    int p = 0;
    char prefix[INPUT_MAX];
    build_prompt(prefix, sizeof(prefix));
    int prefix_len = s_len(prefix);

    for (int i = 0; i < prefix_len && p < (int)sizeof(prompt) - 1; i++) {
        prompt[p++] = prefix[i];
    }
    
    int max_visible = ((TERM_INPUT_W - 8) / 8) - 1 - prefix_len;
    if (max_visible < 1) max_visible = 1;
    int start = (input_len > max_visible) ? (input_len - max_visible) : 0;
    
    for (int i = start; i < input_len && p < INPUT_MAX; i++) {
        prompt[p++] = input_buf[i];
    }
    prompt[p] = '\0';
    
    ui_draw_input(TERM_INPUT_X, TERM_INPUT_Y, TERM_INPUT_W, TERM_INPUT_H, prompt, is_focused);
}

static char* ltrim(char* s) {
    while (*s == ' ') s++;
    return s;
}

static void rtrim(char* s) {
    int n = s_len(s);
    while (n > 0 && s[n - 1] == ' ') {
        s[n - 1] = '\0';
        n--;
    }
}

static void launch_and_report(const char* filename) {
    write_text("Launching ");
    write_text(filename);
    push_char('\n');
    if (launch_tsk(filename)) {
        write_line("Process started/focused.");
    } else {
        write_line("Launch failed.");
    }
}

static void print_ls(int allow_hidden) {
    char list_buf[512];
    list_buf[0] = '\0';
    if (!list_files_at(list_buf, sizeof(list_buf), current_dir[0] ? current_dir : 0)) {
        write_line("ls failed.");
        return;
    }
    if (list_buf[0] == '\0') {
        write_line("(no files)");
        return;
    }

    int shown = 0;
    int i = 0;
    while (list_buf[i]) {
        char name[96];
        int p = 0;

        while (list_buf[i] && list_buf[i] != '\n') {
            if (p < (int)sizeof(name) - 1) {
                name[p++] = list_buf[i];
            }
            i++;
        }
        if (list_buf[i] == '\n') i++;
        name[p] = '\0';

        if (name[0] == '\0') continue;

        char shown_name[112];
        format_visible_name(name, allow_hidden, shown_name, sizeof(shown_name));
        if (shown_name[0] == '\0') continue;

        write_line(shown_name);
        shown++;
    }

    if (!shown) {
        write_line("(no files)");
    }
}

static void run_command_core(char* c, int allow_hidden) {
    if (s_cmp(c, "help") == 0) {
        write_line("Commands:");
        write_line("help  version  cls  ls  cd <dir>");
        write_line("cat <file>  run <name>");
        write_line("net  ip <a.b.c.d>  gw <a.b.c.d>");
        write_line("dnsip <a.b.c.d>  ping <a.b.c.d>");
        write_line("dns <host>  http <host> [path]");
        write_line("<name.tsk>  exit");
        write_line("sudo  sudo off  sudo <cmd>");
        return;
    }

    if (s_cmp(c, "cls") == 0) {
        clear_output();
        return;
    }

    if (s_cmp(c, "version") == 0) {
        print_system_version();
        return;
    }

    if (s_cmp(c, "ls") == 0) {
        print_ls(allow_hidden);
        return;
    }

    if (s_cmp(c, "net") == 0) {
        print_net_info();
        return;
    }

    if (s_cmp(c, "ip") == 0) {
        write_line("Usage: ip <a.b.c.d>");
        return;
    }

    if (s_cmp(c, "gw") == 0) {
        write_line("Usage: gw <a.b.c.d>");
        return;
    }

    if (s_cmp(c, "dnsip") == 0) {
        write_line("Usage: dnsip <a.b.c.d>");
        return;
    }

    if (s_cmp(c, "ping") == 0) {
        write_line("Usage: ping <a.b.c.d>");
        return;
    }

    if (s_cmp(c, "dns") == 0) {
        write_line("Usage: dns <host>");
        return;
    }

    if (s_cmp(c, "http") == 0) {
        write_line("Usage: http <host> [path]");
        return;
    }

    if (s_ncmp(c, "ip ", 3) == 0 || s_ncmp(c, "gw ", 3) == 0) {
        unsigned char ip[4];
        char* arg = ltrim(c + 3);
        int is_gw = (c[0] == 'g');

        if (!parse_ipv4_text(arg, ip)) {
            if (is_gw) {
                write_line("Usage: gw <a.b.c.d>");
            } else {
                write_line("Usage: ip <a.b.c.d>");
            }
            return;
        }

        if (is_gw) {
            net_set_gateway(ip[0], ip[1], ip[2], ip[3]);
            write_text("Gateway set: ");
        } else {
            net_set_local_ip(ip[0], ip[1], ip[2], ip[3]);
            write_text("IP set: ");
        }
        write_ipv4_addr(ip);
        push_char('\n');
        return;
    }

    if (s_ncmp(c, "dnsip ", 6) == 0) {
        unsigned char ip[4];
        char* arg = ltrim(c + 6);
        if (!parse_ipv4_text(arg, ip)) {
            write_line("Usage: dnsip <a.b.c.d>");
            return;
        }

        net_set_dns(ip[0], ip[1], ip[2], ip[3]);
        write_text("DNS set: ");
        write_ipv4_addr(ip);
        push_char('\n');
        return;
    }

    if (s_ncmp(c, "ping ", 5) == 0) {
        unsigned char ip[4];
        char* arg = ltrim(c + 5);

        if (!parse_ipv4_text(arg, ip)) {
            write_line("Usage: ping <a.b.c.d>");
            return;
        }

        write_text("Ping ");
        write_ipv4_addr(ip);
        write_text(" ... ");
        if (net_ping(ip[0], ip[1], ip[2], ip[3])) {
            write_line("OK");
        } else {
            write_line("FAILED");
        }
        return;
    }

    if (s_ncmp(c, "dns ", 4) == 0) {
        unsigned char ip[4];
        char* host = ltrim(c + 4);

        if (host[0] == '\0') {
            write_line("Usage: dns <host>");
            return;
        }

        if (!net_dns_query(host, ip)) {
            write_line("DNS failed.");
            return;
        }

        write_text(host);
        write_text(" -> ");
        write_ipv4_addr(ip);
        push_char('\n');
        return;
    }

    if (s_ncmp(c, "http ", 5) == 0) {
        char* host = ltrim(c + 5);
        char* path;
        char* body;

        if (host[0] == '\0') {
            write_line("Usage: http <host> [path]");
            return;
        }

        path = host;
        while (*path && *path != ' ') path++;
        if (*path) {
            *path++ = '\0';
            path = ltrim(path);
        }
        if (!*path) path = "/";

        if (!net_http_get(host, path, net_http_buf, sizeof(net_http_buf), 0)) {
            write_line("HTTP failed.");
            return;
        }

        body = find_http_body(net_http_buf);
        if (!body || body[0] == '\0') {
            write_line("(empty body)");
            return;
        }

        write_text(body);
        if (body[s_len(body) - 1] != '\n') push_char('\n');
        return;
    }

    if (s_cmp(c, "cd") == 0) {
        current_dir[0] = '\0';
        print_cwd();
        return;
    }

    if (s_ncmp(c, "cd ", 3) == 0) {
        char* dir = ltrim(c + 3);
        char target_dir[INPUT_MAX];
        char probe[2];

        if (dir[0] == '\0') {
            current_dir[0] = '\0';
            print_cwd();
            return;
        }

        if (s_cmp(dir, "/") == 0 || s_cmp(dir, "..") == 0) {
            current_dir[0] = '\0';
            print_cwd();
            return;
        }

        if (s_cmp(dir, ".") == 0) {
            print_cwd();
            return;
        }

        strip_leading_slash(dir, target_dir, sizeof(target_dir));
        probe[0] = '\0';
        if (!list_files_at(probe, sizeof(probe), target_dir)) {
            write_line("No such dir.");
            return;
        }

        s_copy(current_dir, target_dir, sizeof(current_dir));
        print_cwd();
        return;
    }

    if (s_ncmp(c, "cat ", 4) == 0) {
        char* name = ltrim(c + 4);
        if (name[0] == '\0') {
            write_line("Usage: cat <filename>");
            return;
        }

        char real_name[INPUT_MAX];
        if (!map_input_name_to_real(name, allow_hidden, real_name, sizeof(real_name))) {
            write_line("Permission denied.");
            return;
        }

        char file_buf[FILE_BUF_SIZE];
        int n = read_file(real_name, file_buf);
        if (n <= 0) {
            write_line("Read failed.");
            return;
        }
        if (n >= FILE_BUF_SIZE) n = FILE_BUF_SIZE - 1;
        file_buf[n] = '\0';
        write_text(file_buf);
        if (file_buf[n - 1] != '\n') push_char('\n');
        return;
    }

    if (s_ncmp(c, "run ", 4) == 0) {
        char* name = ltrim(c + 4);
        if (name[0] == '\0') {
            write_line("Usage: run <name>");
            return;
        }

        char real_name[INPUT_MAX];
        if (!map_input_name_to_real(name, allow_hidden, real_name, sizeof(real_name))) {
            write_line("Permission denied.");
            return;
        }

        launch_and_report(real_name);
        return;
    }

    if (s_cmp(c, "exit") == 0) {
        write_line("Bye.");
        render();
        exit();
        return;
    }

    {
        char real_name[INPUT_MAX];
        if (map_input_name_to_real(c, allow_hidden, real_name, sizeof(real_name)) &&
            s_ends_with(real_name, ".tsk")) {
            launch_and_report(real_name);
            return;
        }
    }

    if (s_ends_with(c, ".tsk") || s_ends_with(c, HIDDEN_SUFFIX)) {
        write_line("Permission denied.");
        return;
    }

    write_text("Unknown command: ");
    write_line(c);
}

static void run_command(const char* raw_cmd) {
    char cmd[INPUT_MAX];
    s_copy(cmd, raw_cmd, INPUT_MAX);

    char* c = ltrim(cmd);
    rtrim(c);
    if (c[0] == '\0') return;

    if (s_cmp(c, "sudo") == 0) {
        sudo_mode = 1;
        write_line("sudo mode: ON");
        return;
    }

    if (s_cmp(c, "sudo off") == 0) {
        sudo_mode = 0;
        write_line("sudo mode: OFF");
        return;
    }

    if (s_ncmp(c, "sudo ", 5) == 0) {
        char* sub = ltrim(c + 5);
        if (sub[0] == '\0') {
            write_line("Usage: sudo <command>");
            return;
        }
        run_command_core(sub, 1);
        return;
    }

    run_command_core(c, sudo_mode);
}

void main() {
    set_sandbox(1);
    win_set_title("terminal.tsk");

    clear_output();
    input_buf[0] = '\0';
    input_len = 0;
    write_line("TSK terminal ready.");
    write_line("Type help for commands.");
    write_line("Hidden files require sudo.");

    is_focused = win_is_focused();
    render();

    while (1) {
        int mx, my;
        if (get_mouse_click(&mx, &my)) {
            // 检查是不是点击了滑块区域
            int slider_x = TERM_CLIENT_W - TERM_SCROLL_W;
            int slider_y = 2;
            int slider_w = TERM_SCROLL_W;
            int slider_h = TERM_INPUT_Y - slider_y - 2;
            
            if (ui_is_clicked(mx, my, slider_x, slider_y, slider_w, slider_h)) {
                // 计算新的滚动位置
                int max_view = history_count - TERM_OUTPUT_ROWS;
                if (max_view > 0) {
                    // 简单的比例映射：滑块内部点击的 Y 坐标映射到 view_start_row
                    int relative_y = my - slider_y;
                    view_start_row = (relative_y * max_view) / slider_h;
                    
                    if (view_start_row < 0) view_start_row = 0;
                    if (view_start_row > max_view) view_start_row = max_view;
                    render();
                }
            }
        }

        int ev = win_get_event();
        if (ev & WIN_EVENT_FOCUS_CHANGED) {
            is_focused = win_is_focused();
            render();
        }

        if (!(ev & WIN_EVENT_KEY_READY)) continue;

        int key = get_key();
        if (!key) continue;

        // 支持键盘上下键滚动历史 (假设按键码, 根据 ps2.c 可能是特定值)
        // 在目前的 get_key() 实现中，通常只有可打印字符，但我们可以预留支持。

        if (key == '\n') {
            {
                char prefix[INPUT_MAX];
                build_prompt(prefix, sizeof(prefix));
                write_text(prefix);
            }
            write_text(input_buf);
            push_char('\n');
            run_command(input_buf);
            input_len = 0;
            input_buf[0] = '\0';
            render();
            continue;
        }

        if (key == '\b') {
            if (input_len > 0) {
                input_len--;
                input_buf[input_len] = '\0';
            }
            render();
            continue;
        }

        if (key >= 32 && key <= 126 && input_len < INPUT_MAX - 1) {
            input_buf[input_len++] = (char)key;
            input_buf[input_len] = '\0';
            render();
        }
    }
}
