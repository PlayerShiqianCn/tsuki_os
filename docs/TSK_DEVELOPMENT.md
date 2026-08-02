# TSK 应用开发指南

本指南说明如何为 Tsuki OS 编写、构建、打包和调试 `.tsk` 应用。现有最小示例位于 [`apps/app.c`](../apps/app.c)，较完整的窗口应用可参考 [`apps/terminal_tsk.c`](../apps/terminal_tsk.c) 和 [`apps/image_tsk.c`](../apps/image_tsk.c)。

## 1. TSK 是什么

TSK 是 Tsuki OS 的用户态应用镜像格式。构建过程分为两步：

1. 将应用目标文件与 `userspace/lib.o`、可选的 `userspace/ui.o` 链接成固定虚拟地址的 i386 ELF。
2. 使用 `tools/make_tsk` 把 ELF 的可加载段封装成带 `TSK2` 头、入口偏移、镜像大小和校验和的 `.tsk` 文件。

应用启动时，内核保持 ELF 的链接虚拟地址不变，但为每个实例分配独立页目录和私有物理镜像槽。因此同一 `.tsk` 可以同时运行多个实例，且各实例的全局变量互不覆盖。

当前约束：

- freestanding 32 位 i386，无标准 C 运行库和动态链接器；
- 单个应用镜像不得超过 `MP_APP_SLOT_SIZE`，当前为 256 KiB；
- 链接地址必须按应用槽大小对齐，并位于 `MP_APP_SLOT_BASE` 到 `MP_APP_SLOT_LIMIT` 之间；
- 应用当前与内核处于相同 CPU 特权级，`set_sandbox()` 是系统调用策略边界，不是硬件安全边界；
- 应用应通过 [`include/lib.h`](../include/lib.h) 和 [`include/tlx.h`](../include/tlx.h) 使用系统服务，不要直接访问内核内部结构。

## 2. 最小应用

新建 `apps/hello_tsk.c`：

```c
#include "lib.h"

void main(void);

__attribute__((naked)) void _start(void) {
    __asm__ volatile (
        "call main\n\t"
        "call exit\n\t"
        :
        :
        : "memory"
    );
}

static void render(void) {
    draw_rect(0, 0, 216, 138, 15);
    draw_text(12, 16, "Hello from hello.tsk", 0);
    draw_text(12, 32, "Press Q to exit", 0);
}

void main(void) {
    set_sandbox(1);              /* SANDBOX_BASIC */
    win_set_title("Hello TSK");
    render();

    while (1) {
        sleep(1);                /* 让出 CPU，避免忙轮询 */

        int events = win_get_event();
        if (events & WIN_EVENT_FOCUS_CHANGED) {
            render();
        }
        if (events & WIN_EVENT_KEY_READY) {
            int key = get_key();
            if (key == 'q' || key == 27) exit();
        }
    }
}
```

`_start` 必须提供明确入口。`main()` 返回后调用 `exit()`，避免执行到未定义地址。

## 3. 窗口和输入模型

内核启动 TSK 时会先创建默认窗口，并把它绑定到进程。应用通常只需：

```c
win_set_title("My App");
```

如果需要指定尺寸，可在应用启动后替换默认窗口：

```c
if (!win_create(40, 24, 240, 170, "My App")) {
    exit();
}
```

绘图坐标和鼠标坐标都以窗口客户区左上角为原点。常用接口：

- `draw_rect()`：16 色调色板矩形；
- `draw_rect_rgb()`：单个 RGB 矩形；
- `draw_rgb()`：批量 RGB 图像，可在一次系统调用中完成最近邻缩放；
- `draw_text()`：8x8 字体文本；
- `get_mouse_click()`：读取客户区点击；
- `win_get_event()`：读取焦点和键盘就绪事件；
- `sleep()`：阻塞指定 tick，让调度器运行其他任务。

不要在大图像上逐像素调用 `draw_rect_rgb()`。使用 `RgbBlitArgs`：

```c
RgbBlitArgs blit = {
    .pixels = rgb_pixels,
    .src_width = image_w,
    .src_height = image_h,
    .src_row_stride = image_w * 3,
    .src_pixel_stride = 3,
    .dst_x = 8,
    .dst_y = 8,
    .dst_width = 160,
    .dst_height = 100,
};

draw_rgb(&blit);
```

## 4. 文件和 TLX API

简单文件操作可直接使用：

```c
char buffer[128];
int count = read_file("system/version.txt", buffer, sizeof(buffer) - 1);
if (count > 0) buffer[count] = '\0';
```

需要句柄、seek、目录或创建语义时使用 TLX：

```c
int fd = tlx_open("notes.txt", TLX_OPEN_WRITE | TLX_OPEN_CREATE);
if (fd >= 0) {
    tlx_write(fd, "hello\n", 6);
    tlx_close(fd);
}
```

完整接口和当前边界见 [`TLX.md`](../TLX.md)。

## 5. 接入 Makefile

每个不同应用需要一个独立的链接虚拟槽。当前槽位 0 到 5 已由 `app`、`terminal`、`wm`、`start`、`image`、`settings` 使用。新应用可从槽位 6 开始。

在 `Makefile` 中增加：

```make
HELLO_TSK_ADDR = $(call tsk_slot_addr,6)
HELLO_TSK_ELF = $(BUILD_DIR)/hello.tsk.elf
HELLO_TSK = $(BUILD_DIR)/hello.tsk

TSK_APPS += $(HELLO_TSK)

$(HELLO_TSK_ELF): $(BUILD_DIR)/apps/hello_tsk.o \
                  $(BUILD_DIR)/userspace/lib.o \
                  $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	$(CROSS)ld -m elf_i386 -N -e _start -Ttext $(HELLO_TSK_ADDR) -o $@ $^

$(HELLO_TSK): $(HELLO_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@
```

随后把 `$(HELLO_TSK)` 加入 `$(OS_IMAGE)` 的依赖和 `build/mkfs` 参数。若希望放在 `/system` 下并隐藏，可以仿照 `wm.tsk`：

```make
cp -f $(HELLO_TSK) $(FS_ROOT)/system/hello.tsk._hid_
```

并把生成的路径加入 `build/mkfs` 命令。

不要为同一个应用的第二个实例分配第二个链接槽。同一 `.tsk` 的多实例由分页层处理，调用启动 API 即可。

## 6. 注册到开始菜单

静态注册可在 `START_REG` 生成规则中加入：

```make
@printf "Hello|hello.tsk|10\n" >> $@
```

应用也可在运行时注册：

```c
add_start_tile("Hello", "hello.tsk", 10);
```

删除入口：

```c
remove_start_tile("hello.tsk");
```

## 7. 启动和多实例

```c
/* 聚焦已有实例；不存在时创建 */
launch_tsk("hello.tsk");

/* 始终创建新实例 */
launch_tsk_ex("hello.tsk", TSK_LAUNCH_NEW_INSTANCE);
```

终端对应命令：

```text
run hello.tsk
run --new hello.tsk
```

进程信息中的 `instance_id` 和 `window_id` 可用于区分实例。

## 8. 沙箱级别

- `SANDBOX_NONE`：内核进程或完整接口；
- `SANDBOX_BASIC`：普通 GUI、输入、读取、启动和网络接口，限制写文件；
- `SANDBOX_STRICT`：只保留最小绘图、输入和退出能力。

常规应用建议启动时调用：

```c
set_sandbox(SANDBOX_BASIC);
```

如果需要写文件，先确认当前沙箱策略允许对应系统调用。TLX 和直接文件 API 最终都会经过内核检查。

## 9. 构建和运行

```bash
# 构建完整镜像
make -j2

# 启动图形界面
make run

# 同时查看 COM1 日志
make debug

# 运行核心/JPEG/文件系统回归
sh tests/run_regressions.sh
```

生成物：

- `build/hello.tsk.elf`：固定地址 ELF；
- `build/hello.tsk`：带 TSK2 头的应用镜像；
- `build/os-image.img`：包含应用的完整系统镜像。

## 10. 调试检查清单

1. `tools/make_tsk` 是否输出正确的 `load`、`size` 和 `entry`；
2. 镜像大小是否小于等于 256 KiB；
3. 新应用是否使用未占用且对齐的 TSK 槽；
4. `$(HELLO_TSK)` 是否同时进入 `TSK_APPS`、镜像依赖和 mkfs 输入；
5. 事件循环是否调用 `sleep()`；
6. 只在 `WIN_EVENT_KEY_READY` 后调用 `get_key()`；
7. 大图像是否使用 `draw_rgb()`；
8. QEMU 串口是否出现 `page fault`、`bad ctx`、`proc create fail` 或 `win arena full`；
9. 同时启动多个实例后，全局状态、窗口标题和输入是否相互独立；
10. 退出实例后再次启动，窗口页和应用物理槽是否可复用。

## 11. 相关文档

- [项目 README](../README.md)
- [更新日志](../CHANGELOG.md)
- [TLX 兼容层](../TLX.md)
- [工程结构](../PROJECT_STRUCTURE.md)
