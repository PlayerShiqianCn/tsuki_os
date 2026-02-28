// app.c
#include "lib.h"
#include "ui.h"

// 在 bss 段定义栈
unsigned char stack[8192];

static int is_registered = 0;

void main();

// 强制裸函数，不生成 prologue
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

static void render(void) {
    // 渲染背景
    draw_rect(0, 0, 200, 150, 15); // 白色背景
    
    // 渲染指导文本
    draw_text(10, 20, "Start Menu Tile Test", 0);

    // 绘制按钮
    if (is_registered) {
        ui_draw_button(30, 50, 140, 30, "Remove Tile", 4, 15, 0); // 红色背景，白色字体
    } else {
        ui_draw_button(30, 50, 140, 30, "Register Tile", 2, 15, 0); // 绿色背景，白色字体
    }
}

static void sync_registration_state(void) {
    StartTile tiles[16];
    int count = get_start_tiles(tiles, 16);

    is_registered = 0;
    for (int i = 0; i < count; i++) {
        if (tiles[i].file[0] == 'a' &&
            tiles[i].file[1] == 'p' &&
            tiles[i].file[2] == 'p' &&
            tiles[i].file[3] == '.' &&
            tiles[i].file[4] == 't' &&
            tiles[i].file[5] == 's' &&
            tiles[i].file[6] == 'k' &&
            tiles[i].file[7] == '\0') {
            is_registered = 1;
            break;
        }
    }
}

void main() {
    // 1. 进入沙箱
    set_sandbox(1);
    win_set_title("Demo App");

    sync_registration_state();
    
    render();

    // 4. 事件循环
    while(1) {
        int mx, my;
        if (get_mouse_click(&mx, &my)) {
            // 点击了按钮
            if (ui_is_clicked(mx, my, 30, 50, 140, 30)) {
                
                // 绘制按下状态
                if (is_registered) {
                    ui_draw_button(30, 50, 140, 30, "Remove Tile", 4, 15, 1);
                } else {
                    ui_draw_button(30, 50, 140, 30, "Register Tile", 2, 15, 1);
                }
                
                // 执行切换
                if (is_registered) {
                    remove_start_tile("app.tsk");
                } else {
                    add_start_tile("Demo App", "app.tsk", 14);
                }
                sync_registration_state();
                
                // 绘制弹起状态
                render();
            }
        }

        int ev = win_get_event();
        if (ev & 1) { // 焦点改变
            render();
        }
        
        // 可选：键盘退出
        if (ev & 2) {
            int key = get_key();
            if (key == 'q' || key == 27) {
                exit();
            }
        }
    }
}
