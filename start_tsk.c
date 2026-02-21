#include "lib.h"

#define ITEM_COUNT 4
#define TILE_W 86
#define TILE_H 40

typedef struct {
    const char* title;
    const char* file;
    int color;
    int x;
    int y;
} Tile;

static unsigned char stack[4096];
static int selected = 0;
static int is_focused = 0;
static char status_text[32] = "Enter launch | q exit";

static const Tile tiles[ITEM_COUNT] = {
    {"Terminal", "terminal.tsk", 2, 8, 24},
    {"WinMgr",   "wm.tsk",       1, 102, 24},
    {"Start",    "start.tsk",    4, 8, 74},
    {"Demo",     "app.tsk",      14, 102, 74},
};

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

static void set_status(const char* a, const char* b) {
    int p = 0;
    for (int i = 0; a[i] && p < (int)sizeof(status_text) - 1; i++) status_text[p++] = a[i];
    for (int i = 0; b[i] && p < (int)sizeof(status_text) - 1; i++) status_text[p++] = b[i];
    status_text[p] = '\0';
}

static void launch_selected(void) {
    const char* name = tiles[selected].file;
    if (launch_tsk(name)) {
        set_status("Launched/focused: ", name);
    } else {
        set_status("Launch failed: ", name);
    }
}

static void render(void) {
    draw_rect(0, 0, 196, 128, 13);
    draw_rect(0, 0, 196, 14, 1);
    draw_text(4, 3, "start.tsk", 15);
    draw_text(124, 3, is_focused ? "FOCUS" : "BLUR ", is_focused ? 10 : 12);
    draw_text(6, 16, "WASD move | Enter launch", 15);

    for (int i = 0; i < ITEM_COUNT; i++) {
        int x = tiles[i].x;
        int y = tiles[i].y;
        if (i == selected) {
            draw_rect(x - 2, y - 2, TILE_W + 4, TILE_H + 4, 15);
        }
        draw_rect(x, y, TILE_W, TILE_H, tiles[i].color);
        draw_text(x + 6, y + 8, tiles[i].title, 15);
        draw_text(x + 6, y + 22, tiles[i].file, 15);
    }

    draw_rect(0, 118, 196, 10, 8);
    draw_text(2, 119, status_text, 15);
}

void main() {
    set_sandbox(1);
    win_set_title("start.tsk");
    is_focused = win_is_focused();
    render();

    while (1) {
        int ev = win_get_event();
        if (ev & WIN_EVENT_FOCUS_CHANGED) {
            is_focused = win_is_focused();
            render();
        }

        if (!(ev & WIN_EVENT_KEY_READY)) continue;

        int key = get_key();
        if (!key) continue;

        if (key >= '1' && key <= '4') {
            selected = key - '1';
            launch_selected();
            render();
            continue;
        }

        if (key == 'a' || key == 'h') {
            if ((selected % 2) == 1) selected--;
            render();
            continue;
        }
        if (key == 'd' || key == 'l') {
            if ((selected % 2) == 0) selected++;
            render();
            continue;
        }
        if (key == 'w' || key == 'k') {
            if (selected >= 2) selected -= 2;
            render();
            continue;
        }
        if (key == 's' || key == 'j') {
            if (selected <= 1) selected += 2;
            render();
            continue;
        }
        if (key == '\n' || key == ' ') {
            launch_selected();
            render();
            continue;
        }
        if (key == 'q') {
            exit();
            return;
        }
    }
}
