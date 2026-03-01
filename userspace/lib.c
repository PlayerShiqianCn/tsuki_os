// lib.c
#include "syscall.h" // 引用之前的头文件
#include "lib.h"

// 3个参数的系统调用
static inline int _syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    // GCC 内联汇编
    // a=eax (num), b=ebx (arg1), c=ecx (arg2), d=edx (arg3)
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret) 
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3)
        : "memory"
    );
    return ret;
}

// 5个参数的系统调用 (用于画图)
static inline int _syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5) {
    int ret;
    // S=esi, D=edi
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), "S"(arg4), "D"(arg5)
        : "memory"
    );
    return ret;
}

// === 用户 API ===

void exit() {
    _syscall3(SYS_EXIT, 0, 0, 0);
}

void print(const char* str) {
    _syscall3(SYS_PRINT, (int)str, 0, 0);
}

void sleep(int ticks) {
    _syscall3(SYS_SLEEP, ticks, 0, 0);
}

int read_file(const char* filename, void* buffer) {
    return _syscall3(SYS_READ_FILE, (int)filename, (int)buffer, 0);
}

int write_file(const char* filename, const void* buffer, int size) {
    return _syscall3(SYS_WRITE_FILE, (int)filename, (int)buffer, size);
}

void draw_rect(int x, int y, int w, int h, int color) {
    _syscall5(SYS_DRAW_RECT, x, y, w, h, color);
}

void draw_rect_rgb(int x, int y, int w, int h, unsigned int rgb) {
    _syscall5(SYS_DRAW_RECT_RGB, x, y, w, h, (int)rgb);
}

void draw_text(int x, int y, const char* str, int color) {
    _syscall5(SYS_DRAW_TEXT, x, y, (int)str, color, 0);
}

int get_key(void) {
    return _syscall3(SYS_GET_KEY, 0, 0, 0);
}

void set_sandbox(int level) {
    _syscall3(SYS_SET_SANDBOX, level, 0, 0);
}

int win_create(int x, int y, int w, int h, const char* title) {
    return _syscall5(SYS_WIN_CREATE, x, y, w, h, (int)title);
}

int win_set_title(const char* title) {
    return _syscall3(SYS_WIN_SET_TITLE, (int)title, 0, 0);
}

int win_is_focused(void) {
    return _syscall3(SYS_WIN_IS_FOCUSED, 0, 0, 0);
}

int win_get_event(void) {
    return _syscall3(SYS_WIN_GET_EVENT, 0, 0, 0);
}

int list_files(char* buffer, int max_len) {
    return _syscall3(SYS_FS_LIST, (int)buffer, max_len, 0);
}

int list_files_at(char* buffer, int max_len, const char* dir) {
    return _syscall3(SYS_FS_LIST, (int)buffer, max_len, (int)dir);
}

int launch_tsk(const char* filename) {
    return _syscall3(SYS_LAUNCH_TSK, (int)filename, 0, 0);
}

int get_mouse_click(int* x, int* y) {
    int ret, mx, my;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret), "=b" (mx), "=c" (my)
        : "a" (SYS_GET_MOUSE_EVENT)
        : "memory", "edx"
    );
    if (ret) {
        if (x) *x = mx;
        if (y) *y = my;
    }
    return ret;
}

int add_start_tile(const char* title, const char* file, int color) {
    return _syscall3(SYS_ADD_START_TILE, (int)title, (int)file, color);
}

int get_start_tiles(StartTile* buffer, int max_count) {
    return _syscall3(SYS_GET_START_TILES, (int)buffer, max_count, 0);
}

int remove_start_tile(const char* file) {
    return _syscall3(SYS_REMOVE_START_TILE, (int)file, 0, 0);
}

int net_get_info(NetInfo* out) {
    return _syscall3(SYS_NET_INFO, (int)out, 0, 0);
}

int net_ping(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    return _syscall5(SYS_NET_PING, (int)a, (int)b, (int)c, (int)d, 0);
}

int net_dns_query(const char* host, unsigned char out_ip[4]) {
    return _syscall3(SYS_NET_DNS_QUERY, (int)host, (int)out_ip, 0);
}

int net_http_get(const char* host, const char* path, char* out, int out_max, int* out_status_code) {
    return _syscall5(SYS_NET_HTTP_GET, (int)host, (int)path, (int)out, out_max, (int)out_status_code);
}

int net_set_local_ip(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    return _syscall5(SYS_NET_SET_LOCAL_IP, (int)a, (int)b, (int)c, (int)d, 0);
}

int net_set_gateway(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    return _syscall5(SYS_NET_SET_GATEWAY, (int)a, (int)b, (int)c, (int)d, 0);
}

int net_set_dns(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    return _syscall5(SYS_NET_SET_DNS, (int)a, (int)b, (int)c, (int)d, 0);
}

int set_wallpaper_style(int style) {
    return _syscall3(SYS_SET_WALLPAPER_STYLE, style, 0, 0);
}

int set_start_page_enabled(int enabled) {
    return _syscall3(SYS_SET_START_PAGE_ENABLED, enabled, 0, 0);
}
