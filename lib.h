#ifndef LIB_H
#define LIB_H

#include "syscall.h"

// API 声明
void exit();
void print(const char* str);
int read_file(const char* filename, void* buffer);
int write_file(const char* filename, const void* buffer, int size);
void draw_rect(int x, int y, int w, int h, int color);
void draw_rect_rgb(int x, int y, int w, int h, unsigned int rgb);
void draw_text(int x, int y, const char* str, int color);
int get_key(void);
void set_sandbox(int level);
int win_create(int x, int y, int w, int h, const char* title);
int win_set_title(const char* title);
int win_is_focused(void);
int win_get_event(void);
int list_files(char* buffer, int max_len);
int list_files_at(char* buffer, int max_len, const char* dir);
int launch_tsk(const char* filename);
int get_mouse_click(int* x, int* y);

// Start Menu Tile API
typedef struct {
    char title[16];
    char file[16];
    int color;
    int x; // 用于渲染排版的预留字段
    int y; // 用于渲染排版的预留字段
} StartTile;

typedef struct {
    int present;
    int initialized;
    int tx_ready;
    int rx_ready;
    unsigned int tx_ok_count;
    unsigned int rx_ok_count;
    unsigned int ping_ok_count;
    unsigned int dns_ok_count;
    unsigned int tcp_ok_count;
    unsigned int curl_ok_count;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char bus;
    unsigned char slot;
    unsigned char func;
    unsigned char irq_line;
    unsigned char mac[6];
    unsigned char local_ip[4];
    unsigned char gateway_ip[4];
    unsigned char dns_ip[4];
} NetInfo;

int add_start_tile(const char* title, const char* file, int color);
int get_start_tiles(StartTile* buffer, int max_count);
int remove_start_tile(const char* file);
int net_get_info(NetInfo* out);
int net_ping(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
int net_dns_query(const char* host, unsigned char out_ip[4]);
int net_http_get(const char* host, const char* path, char* out, int out_max, int* out_status_code);
int net_set_local_ip(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
int net_set_gateway(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
int net_set_dns(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
int set_wallpaper_style(int style);
int set_start_page_enabled(int enabled);

#endif
