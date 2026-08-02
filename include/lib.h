#ifndef LIB_H
#define LIB_H

#include "syscall.h"
#include "tlx.h"

// API 声明
void exit();
void print(const char* str);
void sleep(int ticks);
unsigned int get_ticks(void);
int read_file(const char* filename, void* buffer, unsigned int capacity);
int write_file(const char* filename, const void* buffer, int size);
void draw_rect(int x, int y, int w, int h, int color);
void draw_rect_rgb(int x, int y, int w, int h, unsigned int rgb);
int draw_rgb(const RgbBlitArgs* args);
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
int launch_tsk_ex(const char* filename, int flags);
int get_video_stats(UserVideoStats* out);
int get_mouse_click(int* x, int* y);

// Start Menu Tile API
typedef struct {
    char title[16];
    char file[16];
    int color;
    int x; // 用于渲染排版的预留字段
    int y; // 用于渲染排版的预留字段
} StartTile;

// Process info (for ps command)
typedef struct {
    int pid;
    int parent_pid;
    char name[32];
    int state;
    int priority;
    unsigned int total_ticks;
    unsigned int instance_id;
    int window_id;
} ProcessInfo;


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

// Process management
int get_process_list(ProcessInfo* buffer, int max_count);
int set_process_priority(int pid, int priority);
int get_process_priority(int pid);
// File management
int create_file(const char* path);
int delete_file(const char* path);
int make_dir(const char* path);
// System info
int get_version(char* buffer, int max_len);


#endif
