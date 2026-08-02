# 项目结构

## 目录分层

| 路径 | 功能说明 |
|------|---------|
| `boot/` | 启动引导、内核入口、任务入口汇编，以及链接脚本 |
| `kernel/` | 内核主逻辑：主循环、中断、调度、系统调用、堆、日志 |
| `drivers/` | 设备与平台驱动：PS/2、VBE 显示、窗口、磁盘、PCI、网络、音频 |
| `fs/` | 文件系统实现 |
| `apps/` | 用户任务源码：终端、开始页、窗口管理器、图片、设置等 |
| `userspace/` | 用户态通用库：系统调用封装、UI、JPEG 解码 |
| `include/` | 内核和用户态共享头文件 |
| `tools/` | 构建辅助工具：`mkfs`、`make_tsk`、资源转换脚本 |
| `build/` | 构建过程中生成的目标文件、任务文件和 OS 镜像 |

## 关键文件

| 文件 | 功能说明 |
|------|---------|
| `boot/boot.asm` | 启动引导程序 |
| `boot/kernel_entry.asm` | 内核入口点 |
| `boot/link.ld` | 定义内核链接和内存布局 |
| `kernel/kernel.c` | 内核主程序、桌面与会话主循环 |
| `kernel/process.c` / `include/process.h` | 进程控制、调度、睡眠和唤醒 |
| `kernel/syscall.c` / `include/syscall.h` | 系统调用分发 |
| `kernel/tlx.c` / `include/tlx.h` | TLX 兼容层的内核实现和用户接口 |
| `drivers/video.c` / `include/video.h` | 显卡初始化和像素绘制 |
| `drivers/window.c` / `include/window.h` | 窗口管理和绘制 |
| `fs/fs.c` / `include/fs.h` | 文件系统实现 |
| `tools/mkfs.c` | 镜像文件系统创建工具 |
| `tools/make_tsk.c` | `.tsk` 任务镜像打包器 |
| `userspace/lib.c` / `include/lib.h` | 应用程序库和系统调用封装 |
| `userspace/jpeg.c` / `include/jpeg.h` | JPEG 解析与解码库 |

## 关键数据结构

### Window

```c
typedef struct Window {
    int id;
    int x, y, w, h;
    char* title;
    int visible;
    unsigned char bg_color;
    unsigned char* back_buffer;
    void (*extra_draw)(struct Window*);
} Window;
```

### Process

```c
typedef struct Process {
    int pid;
    unsigned int esp;
    unsigned int stack_base;
    ProcessState state;
    Window* win;
    struct Process* next;
} Process;
```

### HeapBlock

```c
typedef struct {
    unsigned int size;
    int is_free;
    struct HeapBlock* next;
} HeapBlock;
```

## 内存布局

- `0x00000000-0x00000FFF`：中断向量表（IVT）
- `0x00001000-0x0009FFFF`：传统 RAM
- `0x000A0000-0x000BFFFF`：VGA 显存
- `0x00100000` 以上：内核代码和堆
- 其余内核保留区、日志区、应用槽位和窗口缓冲地址由 `include/mp.h` 统一定义。

## 技术信息

- **语言**：C、x86-32 汇编
- **目标平台**：x86 32 位 PC
- **模拟环境**：QEMU
- **文件系统**：ext2
- **显示模式**：VBE 32 位线性帧缓冲，支持 `640x480`、`800x600`、`1024x768`

## 当前限制

1. 应用和窗口系统仍使用 `320x200` 逻辑绘制坐标，高分辨率模式通过缩放输出。
2. 网络栈只覆盖基础 `ping`、DNS 和简单 HTTP，不支持 HTTPS/TLS。
3. 当前没有虚拟内存和分页支持。
4. 目录仅支持单级（根目录下），暂不支持多级嵌套目录的创建与遍历。
5. 文件系统单块组设计，最大支持约 8MB 数据区。

## 后续方向

- 实现虚拟内存和分页
- 支持多级嵌套目录遍历
- 增加文件重命名和属性修改
- 实现设备驱动框架
- 支持用户态和内核态分离
- 优化图形渲染性能
- 扩展网络栈支持 TCP 连接复用和 HTTPS
