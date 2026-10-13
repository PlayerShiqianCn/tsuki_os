// terminal_cmd.c — 终端命令层：表驱动分发 + 各命令实现 + 路径/网络工具。
// 输出层（push_char/write_text/write_line/clear_output/render）与共享状态
// （current_dir/sudo_mode）在 terminal_tsk.c，声明见 terminal_internal.h。

#include "terminal_internal.h"

static char net_http_buf[NET_HTTP_BUF_SIZE];

// ==== 数字格式化输出 ====

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

static void write_int(int value) {
    if (value < 0) {
        push_char('-');
        write_uint((unsigned int)(-value));
    } else {
        write_uint((unsigned int)value);
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

// ==== 文本工具 ====

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char* ltrim(char* s) {
    while (*s == ' ') s++;
    return s;
}

static void rtrim(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] == ' ') {
        s[n - 1] = '\0';
        n--;
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

// ==== 路径与隐藏文件 ====

static int is_hidden_name(const char* name) {
    return str_ends_with(name, HIDDEN_SUFFIX);
}

static int has_slash(const char* s) {
    for (int i = 0; s && s[i]; i++) {
        if (s[i] == '/') return 1;
    }
    return 0;
}

static void strip_leading_slash(const char* src, char* dst, int max) {
    while (src && *src == '/') src++;
    strlcpy(dst, src ? src : "", max);
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
        strlcpy(name, path, name_max);
        return;
    }

    if (last_slash >= dir_max) last_slash = dir_max - 1;
    for (i = 0; i < last_slash; i++) dir[i] = path[i];
    dir[last_slash] = '\0';
    strlcpy(name, path + last_slash + 1, name_max);
}

// 把用户输入解析为绝对路径；"." 与 ".." 不跨级解析（文件系统仅根寻址）
static void resolve_input_path(const char* input, char* out, int out_max) {
    char trimmed[INPUT_MAX];

    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!input) return;

    strip_leading_slash(input, trimmed, sizeof(trimmed));

    if (trimmed[0] == '\0' || strcmp(trimmed, ".") == 0) {
        strlcpy(out, current_dir, out_max);
        return;
    }
    if (strcmp(trimmed, "..") == 0) {
        out[0] = '\0';
        return;
    }

    if (current_dir[0] && !has_slash(trimmed)) {
        strlcpy(out, current_dir, out_max);
        strlcat(out, "/", out_max);
        strlcat(out, trimmed, out_max);
        return;
    }

    strlcpy(out, trimmed, out_max);
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

        if (strcmp(item, name) == 0) return 1;
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
        strlcpy(out, real_name, out_max);
        return;
    }

    strlcpy(out, real_name, out_max);
}

static int map_input_name_to_real(const char* input_name, int allow_hidden, char* out_real, int out_max) {
    char resolved[INPUT_MAX];

    if (!input_name || !out_real || out_max <= 0) return 0;

    resolve_input_path(input_name, resolved, sizeof(resolved));
    if (resolved[0] == '\0') return 0;
    strlcpy(out_real, resolved, out_max);

    if (is_hidden_name(out_real) && !allow_hidden) {
        return 0;
    }

    if (!allow_hidden) {
        char hidden_candidate[INPUT_MAX];
        strlcpy(hidden_candidate, out_real, sizeof(hidden_candidate));
        strlcat(hidden_candidate, HIDDEN_SUFFIX, sizeof(hidden_candidate));
        if (path_exists(hidden_candidate)) return 0;
    } else if (!is_hidden_name(out_real) && !path_exists(out_real)) {
        char hidden_candidate[INPUT_MAX];
        strlcpy(hidden_candidate, out_real, sizeof(hidden_candidate));
        strlcat(hidden_candidate, HIDDEN_SUFFIX, sizeof(hidden_candidate));
        if (path_exists(hidden_candidate)) {
            strlcpy(out_real, hidden_candidate, out_max);
        }
    }

    return 1;
}

// ==== 各命令实现 ====

static int read_version_field(const char* text, const char* key, char* out, int out_max) {
    int pos = 0;
    int key_len;

    if (!text || !key || !out || out_max <= 0) return 0;
    out[0] = '\0';
    key_len = (int)strlen(key);
    if (key_len <= 0) return 0;

    while (text[pos]) {
        int line_start = pos;
        int out_i = 0;

        while (text[pos] && text[pos] != '\n' && text[pos] != '\r') pos++;
        if (strncmp(text + line_start, key, key_len) == 0 && text[line_start + key_len] == '=') {
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
    int n = read_file("system/version.txt", file_buf, sizeof(file_buf) - 1);
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

static void print_ps_rows(void) {
    ProcessInfo procs[16];
    int count = get_process_list(procs, 16);
    int i;

    if (count <= 0) {
        write_line("No processes.");
        return;
    }

    write_text("PID  PPRI  STATE  TICKS  NAME");
    push_char('\n');
    for (i = 0; i < count; i++) {
        write_uint(procs[i].pid);
        push_char(' ');
        if (procs[i].pid < 10) push_char(' ');

        write_int(procs[i].priority);
        push_char(' ');
        if (procs[i].priority >= 0 && procs[i].priority < 10) push_char(' ');

        switch (procs[i].state) {
            case 0: write_text("READY   "); break;
            case 1: write_text("RUNNING "); break;
            case 2: write_text("BLOCKED "); break;
            case 3: write_text("DEAD    "); break;
            default: write_text("UNKNOWN "); break;
        }

        write_uint(procs[i].total_ticks);
        push_char(' ');
        write_line(procs[i].name);
    }
}

static void print_ls_rows(int allow_hidden) {
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

static void launch_and_report_ex(const char* filename, int flags) {
    write_text("Launching ");
    write_text(filename);
    push_char('\n');
    if (launch_tsk_ex(filename, flags)) {
        write_line((flags & TSK_LAUNCH_NEW_INSTANCE) ? "New instance started." :
                   "Process started/focused.");
    } else {
        write_line("Launch failed.");
    }
}

static void launch_and_report(const char* filename) {
    launch_and_report_ex(filename, TSK_LAUNCH_ACTIVATE);
}

// ==== 命令表 ====

typedef struct {
    const char* name;
    void (*fn)(char* args, int allow_hidden);
} Command;

static void cmd_help(char* args, int allow_hidden);
static void cmd_clear(char* args, int allow_hidden);
static void cmd_pwd(char* args, int allow_hidden);
static void cmd_echo(char* args, int allow_hidden);
static void cmd_uptime(char* args, int allow_hidden);
static void cmd_version(char* args, int allow_hidden);
static void cmd_ps(char* args, int allow_hidden);
static void cmd_sysinfo(char* args, int allow_hidden);
static void cmd_ls(char* args, int allow_hidden);
static void cmd_net(char* args, int allow_hidden);
static void cmd_mkdir(char* args, int allow_hidden);
static void cmd_touch(char* args, int allow_hidden);
static void cmd_rm(char* args, int allow_hidden);
static void cmd_rename(char* args, int allow_hidden);
static void cmd_cd(char* args, int allow_hidden);
static void cmd_cat(char* args, int allow_hidden);
static void cmd_run(char* args, int allow_hidden);
static void cmd_ip(char* args, int allow_hidden);
static void cmd_gw(char* args, int allow_hidden);
static void cmd_dnsip(char* args, int allow_hidden);
static void cmd_ping(char* args, int allow_hidden);
static void cmd_dns(char* args, int allow_hidden);
static void cmd_http(char* args, int allow_hidden);
static void cmd_exit(char* args, int allow_hidden);

static const Command commands[] = {
    {"help",    cmd_help},
    {"cls",     cmd_clear},
    {"clear",   cmd_clear},
    {"pwd",     cmd_pwd},
    {"echo",    cmd_echo},
    {"uptime",  cmd_uptime},
    {"version", cmd_version},
    {"ps",      cmd_ps},
    {"tasks",   cmd_ps},
    {"sysinfo", cmd_sysinfo},
    {"ls",      cmd_ls},
    {"net",     cmd_net},
    {"mkdir",   cmd_mkdir},
    {"touch",   cmd_touch},
    {"rm",      cmd_rm},
    {"rename",  cmd_rename},
    {"cd",      cmd_cd},
    {"cat",     cmd_cat},
    {"run",     cmd_run},
    {"ip",      cmd_ip},
    {"gw",      cmd_gw},
    {"dnsip",   cmd_dnsip},
    {"ping",    cmd_ping},
    {"dns",     cmd_dns},
    {"http",    cmd_http},
    {"exit",    cmd_exit},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    write_line("Commands:");
    write_line("help  version  pwd  cls/clear  ls");
    write_line("cd <dir>  cat <file>  run [--new] <name>");
    write_line("echo <text>  uptime  sysinfo  ps");
    write_line("mkdir <dir>  touch <file>  rm <file>");
    write_line("rename <old> <new>");
    write_line("net  ip <a.b.c.d>  gw <a.b.c.d>");
    write_line("dnsip <a.b.c.d>  ping <a.b.c.d>");
    write_line("dns <host>  http <host> [path]");
    write_line("<name.tsk>  exit");
    write_line("sudo  sudo off  sudo <cmd>");
}

static void cmd_clear(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    clear_output();
}

static void cmd_pwd(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    print_cwd();
}

static void cmd_echo(char* args, int allow_hidden) {
    (void)allow_hidden;
    write_line(args);
}

static void cmd_uptime(char* args, int allow_hidden) {
    unsigned int ticks;

    (void)args;
    (void)allow_hidden;
    ticks = get_ticks();
    write_text("Uptime ");
    write_uint(ticks / 100);
    write_text(" s (");
    write_uint(ticks);
    write_line(" ticks)");
}

static void cmd_version(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    print_system_version();
}

static void cmd_ps(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    print_ps_rows();
}

static void cmd_sysinfo(char* args, int allow_hidden) {
    char ver[64];
    unsigned int ticks;
    UserVideoStats video_stats;

    (void)args;
    (void)allow_hidden;
    write_line("=== Tsuki OS System Info ===");

    if (get_version(ver, sizeof(ver)) > 0) {
        write_text("Version:  ");
        write_line(ver);
    }

    ticks = get_ticks();
    write_text("Uptime:   ");
    write_uint(ticks / 100);
    write_line(" s");

    if (get_video_stats(&video_stats)) {
        write_text("Frames:   "); write_uint(video_stats.frames); push_char('\n');
        write_text("Damage px:"); write_uint(video_stats.damaged_logical_pixels); push_char('\n');
        write_text("FB writes:"); write_uint(video_stats.framebuffer_pixels_written); push_char('\n');
        write_text("Idle hlt: "); write_uint(video_stats.idle_halts); push_char('\n');
    }

    write_line("");
    write_line("Processes:");
    print_ps_rows();
}

static void cmd_ls(char* args, int allow_hidden) {
    (void)args;
    print_ls_rows(allow_hidden);
}

static void cmd_net(char* args, int allow_hidden) {
    NetInfo info;

    (void)args;
    (void)allow_hidden;
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

static void cmd_mkdir(char* args, int allow_hidden) {
    char real_name[INPUT_MAX];
    int result;

    if (args[0] == '\0') {
        write_line("Usage: mkdir <dirname>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, real_name, sizeof(real_name))) {
        write_line("Permission denied.");
        return;
    }

    result = make_dir(real_name);
    if (result > 0) {
        write_text("Created directory: ");
        write_line(real_name);
    } else if (result < 0) {
        write_line("Already exists.");
    } else {
        write_line("Failed to create directory.");
    }
}

static void cmd_touch(char* args, int allow_hidden) {
    char real_name[INPUT_MAX];
    int result;

    if (args[0] == '\0') {
        write_line("Usage: touch <filename>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, real_name, sizeof(real_name))) {
        write_line("Permission denied.");
        return;
    }

    result = create_file(real_name);
    if (result > 0) {
        write_text("Created file: ");
        write_line(real_name);
    } else if (result < 0) {
        write_line("Already exists.");
    } else {
        write_line("Failed to create file.");
    }
}

static void cmd_rm(char* args, int allow_hidden) {
    char real_name[INPUT_MAX];
    int result;

    if (args[0] == '\0') {
        write_line("Usage: rm <filename>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, real_name, sizeof(real_name))) {
        write_line("Permission denied.");
        return;
    }

    result = delete_file(real_name);
    if (result > 0) {
        write_text("Deleted: ");
        write_line(real_name);
    } else if (result < 0) {
        write_line("Is a directory.");
    } else {
        write_line("No such file.");
    }
}

static void cmd_rename(char* args, int allow_hidden) {
    char* space;
    char* new_name_arg;
    char old_real[INPUT_MAX];
    char new_real[INPUT_MAX];
    int result;

    if (args[0] == '\0') {
        write_line("Usage: rename <old> <new>");
        return;
    }

    space = args;
    while (*space && *space != ' ') space++;
    if (*space == '\0') {
        write_line("Usage: rename <old> <new>");
        return;
    }
    *space = '\0';
    new_name_arg = ltrim(space + 1);
    if (new_name_arg[0] == '\0') {
        write_line("Usage: rename <old> <new>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, old_real, sizeof(old_real))) {
        write_line("Permission denied.");
        return;
    }
    if (!map_input_name_to_real(new_name_arg, allow_hidden, new_real, sizeof(new_real))) {
        write_line("Permission denied.");
        return;
    }

    result = rename_file(old_real, new_real);
    if (result > 0) {
        write_text("Renamed ");
        write_text(old_real);
        write_text(" -> ");
        write_line(new_real);
    } else if (result < 0) {
        write_line("Target already exists.");
    } else {
        write_line("No such file.");
    }
}

static void cmd_cd(char* args, int allow_hidden) {
    char target_dir[INPUT_MAX];
    char probe[2];

    (void)allow_hidden;
    if (args[0] == '\0' || strcmp(args, "/") == 0 || strcmp(args, "..") == 0) {
        current_dir[0] = '\0';
        print_cwd();
        return;
    }

    if (strcmp(args, ".") == 0) {
        print_cwd();
        return;
    }

    strip_leading_slash(args, target_dir, sizeof(target_dir));
    probe[0] = '\0';
    if (!list_files_at(probe, sizeof(probe), target_dir)) {
        write_line("No such dir.");
        return;
    }

    strlcpy(current_dir, target_dir, sizeof(current_dir));
    print_cwd();
}

static void cmd_cat(char* args, int allow_hidden) {
    char real_name[INPUT_MAX];
    char file_buf[FILE_BUF_SIZE];
    int n;

    if (args[0] == '\0') {
        write_line("Usage: cat <filename>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, real_name, sizeof(real_name))) {
        write_line("Permission denied.");
        return;
    }

    n = read_file(real_name, file_buf, sizeof(file_buf) - 1);
    if (n <= 0) {
        write_line("Read failed.");
        return;
    }
    if (n >= FILE_BUF_SIZE) n = FILE_BUF_SIZE - 1;
    file_buf[n] = '\0';
    write_text(file_buf);
    if (file_buf[n - 1] != '\n') push_char('\n');
}

static void cmd_run(char* args, int allow_hidden) {
    int launch_flags = TSK_LAUNCH_ACTIVATE;
    char real_name[INPUT_MAX];

    if (strncmp(args, "--new ", 6) == 0) {
        launch_flags = TSK_LAUNCH_NEW_INSTANCE;
        args = ltrim(args + 6);
    }
    if (args[0] == '\0') {
        write_line("Usage: run [--new] <name>");
        return;
    }

    if (!map_input_name_to_real(args, allow_hidden, real_name, sizeof(real_name))) {
        write_line("Permission denied.");
        return;
    }

    launch_and_report_ex(real_name, launch_flags);
}

// ip/gw 共用实现，仅提示文案不同
static void do_set_ip(char* args, int is_gw) {
    unsigned char ip[4];

    if (!parse_ipv4_text(args, ip)) {
        write_line(is_gw ? "Usage: gw <a.b.c.d>" : "Usage: ip <a.b.c.d>");
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
}

static void cmd_ip(char* args, int allow_hidden) {
    (void)allow_hidden;
    do_set_ip(args, 0);
}

static void cmd_gw(char* args, int allow_hidden) {
    (void)allow_hidden;
    do_set_ip(args, 1);
}

static void cmd_dnsip(char* args, int allow_hidden) {
    unsigned char ip[4];

    (void)allow_hidden;
    if (!parse_ipv4_text(args, ip)) {
        write_line("Usage: dnsip <a.b.c.d>");
        return;
    }

    net_set_dns(ip[0], ip[1], ip[2], ip[3]);
    write_text("DNS set: ");
    write_ipv4_addr(ip);
    push_char('\n');
}

static void cmd_ping(char* args, int allow_hidden) {
    unsigned char ip[4];

    (void)allow_hidden;
    if (!parse_ipv4_text(args, ip)) {
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
}

static void cmd_dns(char* args, int allow_hidden) {
    unsigned char ip[4];

    (void)allow_hidden;
    if (args[0] == '\0') {
        write_line("Usage: dns <host>");
        return;
    }

    if (!net_dns_query(args, ip)) {
        write_line("DNS failed.");
        return;
    }

    write_text(args);
    write_text(" -> ");
    write_ipv4_addr(ip);
    push_char('\n');
}

static void cmd_http(char* args, int allow_hidden) {
    char* host = args;
    char* path;
    char* body;

    (void)allow_hidden;
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
    if (body[strlen(body) - 1] != '\n') push_char('\n');
}

static void cmd_exit(char* args, int allow_hidden) {
    (void)args;
    (void)allow_hidden;
    write_line("Bye.");
    render();
    exit();
}

// ==== 分发 ====

static int command_word_len(const char* c) {
    int n = 0;
    while (c[n] && c[n] != ' ') n++;
    return n;
}

static void run_command_core(char* c, int allow_hidden) {
    int wl = command_word_len(c);
    unsigned int i;

    for (i = 0; i < COMMAND_COUNT; i++) {
        // c 的首词长度即 wl；strncmp 相等且表名恰好等长 → 全词匹配
        if (strncmp(c, commands[i].name, wl) == 0 && commands[i].name[wl] == '\0') {
            commands[i].fn(ltrim(c + wl), allow_hidden);
            return;
        }
    }

    {
        char real_name[INPUT_MAX];
        if (map_input_name_to_real(c, allow_hidden, real_name, sizeof(real_name)) &&
            str_ends_with(real_name, ".tsk")) {
            launch_and_report(real_name);
            return;
        }
    }

    if (str_ends_with(c, ".tsk") || str_ends_with(c, HIDDEN_SUFFIX)) {
        write_line("Permission denied.");
        return;
    }

    write_text("Unknown command: ");
    write_line(c);
}

void run_command(const char* raw_cmd) {
    char cmd[INPUT_MAX];
    strlcpy(cmd, raw_cmd, INPUT_MAX);

    char* c = ltrim(cmd);
    rtrim(c);
    if (c[0] == '\0') return;

    if (strcmp(c, "sudo") == 0) {
        sudo_mode = 1;
        write_line("sudo mode: ON");
        return;
    }

    if (strcmp(c, "sudo off") == 0) {
        sudo_mode = 0;
        write_line("sudo mode: OFF");
        return;
    }

    if (strncmp(c, "sudo ", 5) == 0) {
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
