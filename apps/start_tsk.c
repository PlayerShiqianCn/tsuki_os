#include "lib.h"
#include "theme.h"

// 全屏无边框窗口: 640x360 logical desktop
#define CLIENT_W 640
#define CLIENT_H 360

#define MAX_TILES 16
#define TILE_W 132
#define TILE_H 64
#define TILES_PER_ROW 4
#define START_X 28
#define START_Y 82
#define SPACING_X 148
#define SPACING_Y 82

static int selected = 0;
static StartTile all_tiles[MAX_TILES];
static int total_tiles = 0;

void main();

__attribute__((naked)) void _start() {
    __asm__ volatile (
        "call main \n\t"
        "call exit \n\t"
        :
        :
        : "memory"
    );
}

static void launch_and_close(void) {
    if (total_tiles == 0) return;
    const char* name = all_tiles[selected].file;
    launch_tsk_ex(name, TSK_LAUNCH_NEW_INSTANCE);
    exit();
}

static void load_tiles(void) {
    total_tiles = 0;
    
    // 1. 固定添加 Terminal
    StartTile* t = &all_tiles[total_tiles++];
    for(int i=0; i<15 && "Terminal"[i]; i++) t->title[i] = "Terminal"[i];
    for(int i=0; i<15 && "terminal.tsk"[i]; i++) t->file[i] = "terminal.tsk"[i];
    t->color = 2; // Dark Green

    // 2. 从内核获取动态注册的磁贴
    StartTile dyn_tiles[15];
    int count = get_start_tiles(&dyn_tiles[0], 15);
    
    for (int i = 0; i < count && total_tiles < MAX_TILES; i++) {
        all_tiles[total_tiles++] = dyn_tiles[i];
    }
    
    // 计算排版坐标
    for (int i = 0; i < total_tiles; i++) {
        int row = i / TILES_PER_ROW;
        int col = i % TILES_PER_ROW;
        all_tiles[i].x = START_X + col * SPACING_X;
        all_tiles[i].y = START_Y + row * SPACING_Y;
    }
}

static void render(void) {
    draw_rect_rgb(0, 0, CLIENT_W, CLIENT_H, TSUKI_RGB_BG);
    draw_text_rgb(28, 24, "TSK Apps", TSUKI_RGB_TEXT);
    draw_text_rgb(28, 44, "Click | Enter | Arrows", TSUKI_RGB_MUTED);

    for (int i = 0; i < total_tiles; i++) {
        int x = all_tiles[i].x;
        int y = all_tiles[i].y;
        unsigned int surface = i == selected ? TSUKI_RGB_CONTROL_HOVER : TSUKI_RGB_SURFACE;

        if (i == selected) {
            draw_round_rect_rgb(x - 2, y - 2, TILE_W + 4, TILE_H + 4, 7, TSUKI_RGB_ACCENT);
        }

        draw_round_rect_rgb(x, y, TILE_W, TILE_H, 6, surface);
        draw_rect(x + 8, y + 10, 2, TILE_H - 20, all_tiles[i].color);
        draw_text_rgb(x + 16, y + 16, all_tiles[i].title, TSUKI_RGB_TEXT);
        draw_text_rgb(x + 16, y + 34, all_tiles[i].file, TSUKI_RGB_MUTED);
    }
}

void main() {
    set_sandbox(1);
    win_set_title("start.tsk");
    
    load_tiles();
    render();

    while (1) {
        int mx, my;
        sleep(1);
        if (get_mouse_click(&mx, &my)) {
            for (int i = 0; i < total_tiles; i++) {
                if (mx >= all_tiles[i].x && mx < all_tiles[i].x + TILE_W &&
                    my >= all_tiles[i].y && my < all_tiles[i].y + TILE_H) {
                    selected = i;
                    launch_and_close();
                    return;
                }
            }
        }

        int ev = win_get_event();
        if (!(ev & WIN_EVENT_KEY_READY)) continue;

        int key = get_key();
        if (!key) continue;

        if (key == '\n' || key == ' ') {
            launch_and_close();
            return;
        }
        if (key == 'q' || key == 27) {
            exit();
            return;
        }
        
        // 简单的键盘导航 (WASD)
        if (key == 'a' || key == 'h') {
            if (selected > 0) selected--;
            render();
        } else if (key == 'd' || key == 'l') {
            if (selected < total_tiles - 1) selected++;
            render();
        } else if (key == 'w' || key == 'k') {
            if (selected >= TILES_PER_ROW) selected -= TILES_PER_ROW;
            render();
        } else if (key == 's' || key == 'j') {
            if (selected + TILES_PER_ROW < total_tiles) selected += TILES_PER_ROW;
            render();
        }
    }
}
