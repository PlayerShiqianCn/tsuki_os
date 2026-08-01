# TLX 兼容层

Tsuki OS 提供独立命名空间的 Tsuki Linux eXtension（TLX）接口。TLX 借鉴常见操作系统语义，但不复用 Linux 的函数名、头文件布局或 syscall 编号。

用户态代码通过 `include/tlx.h` 使用 `tlx_*` API，内核实现位于 `kernel/tlx.c`。

## 使用示例

```c
#include "lib.h"

int handle = tlx_open("system/version.txt", TLX_OPEN_READ);
char buffer[64];
int count = tlx_read(handle, buffer, sizeof(buffer) - 1);
if (count > 0) buffer[count] = '\0';
tlx_close(handle);
```

## 当前接口

- `tlx_open` / `tlx_close` / `tlx_read` / `tlx_write`
- `tlx_seek` / `tlx_fstat` / `tlx_list`
- `tlx_getpid` / `tlx_getppid` / `tlx_sleep` / `tlx_yield` / `tlx_clock`
- `tlx_getcwd` / `tlx_chdir` / `tlx_spawn` / `tlx_identity`
- 标准句柄 `TLX_STD_INPUT`、`TLX_STD_OUTPUT`、`TLX_STD_ERROR`

## 当前边界

- 文件系统只能打开已有文件。
- `TLX_OPEN_CREATE` 当前返回未实现错误。
- 文件写入使用现有的覆盖式 ext2 文件接口。
- 每个进程最多维护 8 个 TLX 文件句柄。
- 系统同时最多维护 16 个 TLX 进程上下文。

后续可以在不改变 `tlx_*` 用户 API 的情况下加入文件创建、分页内存和网络套接字支持。
