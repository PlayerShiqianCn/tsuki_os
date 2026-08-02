# TLX 兼容层

Tsuki OS 提供独立命名空间的 Tsuki Linux eXtension（TLX）接口。TLX 借鉴常见操作系统语义，但不复用 Linux 的函数名、头文件布局或 syscall 编号。

用户态代码通过 `include/tlx.h` 使用 `tlx_*` API，内核实现位于 `kernel/tlx.c`。

## 使用示例

```c
#include "lib.h"

// 打开已有文件
int handle = tlx_open("system/version.txt", TLX_OPEN_READ);
char buffer[64];
int count = tlx_read(handle, buffer, sizeof(buffer) - 1);
if (count > 0) buffer[count] = '\0';
tlx_close(handle);

// 创建并写入新文件
int fd = tlx_open("notes.txt", TLX_OPEN_WRITE | TLX_OPEN_CREATE);
if (fd >= 0) {
    tlx_write(fd, "Hello!", 6);
    tlx_close(fd);
}

// 创建目录
tlx_mkdir("mydir");

// 删除文件
tlx_unlink("old.txt");
```

## 当前接口

### 文件操作
- `tlx_open` / `tlx_close` / `tlx_read` / `tlx_write`
- `tlx_seek` / `tlx_fstat` / `tlx_list`
- `tlx_unlink` — 删除文件
- `tlx_mkdir` — 创建目录

### 进程管理
- `tlx_getpid` / `tlx_getppid` / `tlx_sleep` / `tlx_yield` / `tlx_clock`
- `tlx_getcwd` / `tlx_chdir` / `tlx_spawn` / `tlx_identity`

### 打开标志
- `TLX_OPEN_READ` / `TLX_OPEN_WRITE` / `TLX_OPEN_CREATE` / `TLX_OPEN_TRUNC` / `TLX_OPEN_APPEND` / `TLX_OPEN_DIR`

### 标准句柄
- `TLX_STD_INPUT`、`TLX_STD_OUTPUT`、`TLX_STD_ERROR`

## 当前边界

- `TLX_OPEN_CREATE` 已实现：自动创建文件后打开。
- 文件写入支持动态块分配：新创建的文件可直接写入，无需 mkfs 预留块。
- `tlx_unlink` 删除文件并释放数据块。
- `tlx_mkdir` 创建目录并初始化 `.` 与 `..` 条目。
- 每个进程最多维护 8 个 TLX 文件句柄。
- 系统同时最多维护 16 个 TLX 进程上下文。
- 目录仅支持根目录下的单级结构。
