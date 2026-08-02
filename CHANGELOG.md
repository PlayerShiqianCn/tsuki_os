# 更新记录

## 2026-08-02 — v0.4.0 "Foundation"

### 文件系统：文件创建与删除

- **新建文件**：`fs_create_file()` 分配空闲 inode、初始化元数据、在父目录中插入目录项，用户可通过 `touch` 命令或 `TLX_OPEN_CREATE` 标志创建新文件。
- **删除文件**：`fs_delete_file()` 释放文件占用的所有数据块（含一级间接块）、回收 inode、移除目录项。
- **创建目录**：`fs_mkdir()` 分配 inode 和数据块，初始化 `.` 与 `..` 条目，更新父目录链接计数。
- **动态块分配**：`fs_write_file()` 重写为支持按需分配数据块的写入路径——新创建的文件不再需要 mkfs 预留块即可写入。
- **底层原语**：新增 `alloc_inode` / `free_inode` / `alloc_block` / `free_block` / `add_dir_entry` / `remove_dir_entry` / `write_superblock` / `write_group_desc`，均操作 ext2 位图与组描述符。

### 进程调度：优先级调度

- **优先级字段**：PCB 新增 `priority`（−10 最高 → +10 最低，默认 0）。
- **调度器升级**：从纯轮转（Round-Robin）升级为优先级 + 同级轮转混合调度（`process_pick_next`）。
- **进程查询**：新增 `process_get_info_list()`，支持用户态获取进程列表（PID、父 PID、名称、状态、优先级、累计 tick）。
- **优先级 API**：新增 `process_set_priority()` / `process_get_priority()`。

### 系统调用扩展

| 编号 | 名称 | 功能 |
|------|------|------|
| 31 | `SYS_PS` | 获取进程列表 |
| 32 | `SYS_MKDIR` | 创建目录 |
| 33 | `SYS_DELETE_FILE` | 删除文件 |
| 34 | `SYS_GET_VERSION` | 获取内核版本字符串 |
| 35 | `SYS_SET_PRIORITY` | 设置进程优先级 |
| 36 | `SYS_GET_PRIORITY` | 查询进程优先级 |
| 37 | `SYS_CREATE_FILE` | 创建空文件 |

### TLX 兼容层增强

- **`TLX_OPEN_CREATE`**：不再返回 `ENOSYS`，实际调用 `fs_create_file` 创建文件后打开。
- **`tlx_unlink`**（`TLX_OP_UNLINK`）：删除文件，返回标准 TLX 错误码。
- **`tlx_mkdir`**（`TLX_OP_MKDIR`）：创建目录。
- **feature_bits**：更新为 `0x1FF`，标志文件创建/删除/目录支持。

### 终端增强

- **`ps` / `tasks`**：列出所有进程的 PID、优先级、状态、累计 tick 和名称。
- **`sysinfo`**：显示系统版本、运行时间和进程列表的综合信息。
- **`mkdir <dir>`**：创建目录。
- **`touch <file>`**：创建空文件。
- **`rm <file>`**：删除文件。
- **`write_int`**：新增带符号整数输出函数，用于显示负优先级。

### 构建与工程

- **版本号**：升级至 `0.4.0-foundation`。
- **编译宏**：Makefile 新增 `-DTSUKI_OS_VERSION`，内核可通过 `SYS_GET_VERSION` 返回编译时版本。
- **代码清理**：移除不再使用的 `next_runnable_from` 和 `inode_data_capacity`。

## 2026-08-01

- **构建体验**：支持自动探测 `i686-linux-gnu-` 交叉工具链；新增 `make debug` 串口调试目标。
- **调试输出**：内核日志支持输出到 COM1，QEMU 下可直接在终端查看。
- **终端增强**：新增 `pwd`、`clear`、`echo`、`uptime` 命令；内核新增 `SYS_GET_TICKS` 系统调用。
- **TLX 兼容层**：新增独立命名空间的 Linux 兼容接口，详见 `TLX.md`。

## 2026-03-01

- **目录结构整理**：重新整理源代码和构建产物的目录布局。
- **任务加载器升级**：`.tsk` 默认使用 `TSK2` 头部加载。
- **调度器增强**：加入阻塞睡眠、定时唤醒和时间片字段，并为异常保存现场增加保护。
- **内存布局统一**：新增 `include/mp.h`，集中管理内核保留区、视频后缓冲、日志区、应用槽位和窗口缓冲地址。
- **内核模块拆分**：从 `kernel/kernel.c` 中拆出 `kernel/config.c` 和 `kernel/desktop.c`。

## 2026-02-28

- **显示系统升级**：从纯 VGA 输出切换到 QEMU `stdvga` 的 32 位 VBE 线性帧缓冲，支持 `640x480`、`800x600` 和 `1024x768`。
- **动态缩放与减闪烁**：保留 `320x200` 逻辑坐标，输出自动缩放到当前物理分辨率；重绘改为脏刷新。
- **系统目录结构**：镜像内新增 `/system` 与 `/image`，统一管理系统配置、注册表、库文件和图片资源。
- **配置系统**：新增 `/system/config.rtsk`，支持壁纸、开始页、网络参数和屏幕分辨率配置，写入后立即热重载。
- **开始页注册表**：新增 `/system/start.rtsk`，用于持久化 Start 页面应用入口。
- **设置应用**：新增 `settings.tsk`，可直接修改并应用常用设置。
- **终端增强**：支持 `sudo`、隐藏文件后缀 `._hid_`、`cd`、当前路径提示符，以及 `ping`、`dns`、`http` 等命令。
- **图片查看器**：新增 `image.tsk`，支持从 `/image` 中选择图片并显示。
- **JPEG 解码库**：新增 `jpeg.tso`，支持 SOF0 baseline JPEG 和 SOF2 progressive JPEG 解码。
- **文件系统写入**：支持覆盖写已有文件，用于持久化 `config.rtsk` 和 `start.rtsk`。
- **网络栈接通**：接入 `e1000` 网卡，支持基础 `ping`、DNS 查询和 HTTP GET。
- **启动加载修正**：bootloader 改为分段读取更大的 `kernel.bin`，避免内核增长后被截断。
