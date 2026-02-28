#include "lib.h"

// 全屏无边框窗口: 320x200
#define CLIENT_W 320
#define CLIENT_H 200

#define MAX_TILES 16
#define TILE_W 120
#define TILE_H 60
#define TILES_PER_ROW 2
#define START_X 20
#define START_Y 50
#define SPACING_X 140
#define SPACING_Y 70

static unsigned char stack[4096];
static int selected = 0;
static StartTile all_tiles[MAX_TILES];
static int total_tiles = 0;

void main();

__attribute__((naked)) void _start() {
    __asm__ volatile (
        "movl %0, %%esp \n\t"
        "call main \n\t"
        "call exit \n\t"
        :
        : "r" (stack + sizeof(stack))
        : "memory"
    );
}

static void launch_and_close(void) {
    if (total_tiles == 0) return;
    const char* name = all_tiles[selected].file;
    launch_tsk(name);
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
    draw_rect(0, 0, CLIENT_W, CLIENT_H, 5); // 紫色背景
    draw_text(20, 14, "TSK Apps", 15);
    draw_text(20, 30, "Click | Enter | Arrows", 15);

    for (int i = 0; i < total_tiles; i++) {
        int x = all_tiles[i].x;
        int y = all_tiles[i].y;

        if (i == selected) {
            draw_rect(x - 3, y - 3, TILE_W + 6, TILE_H + 6, 15);
        }

        draw_rect(x, y, TILE_W, TILE_H, all_tiles[i].color);
        draw_text(x + 12, y + 16, all_tiles[i].title, 15);
        draw_text(x + 12, y + 34, all_tiles[i].file, 15);
    }
}

void main() {
    set_sandbox(1);
    win_set_title("start.tsk");
    
    load_tiles();
    render();

    while (1) {
        int mx, my;
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
