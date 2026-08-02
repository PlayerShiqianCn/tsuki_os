# 更新记录

## 2026-08-02 — v0.4.0 "Foundation"

### 多窗口与进程隔离

- **同应用多实例**：新增 `TSK_LAUNCH_NEW_INSTANCE`，开始菜单和终端 `run --new <name.tsk>` 可创建同一 TSK 的多个独立实例；默认启动分支仍支持聚焦已有实例。
- **进程分页隔离**：PCB 新增 `page_directory`、`app_physical_slot`、`image_inode`、`instance_id`，每个应用使用独立页目录和私有物理镜像槽，修复固定装载地址覆盖运行中代码/数据的问题。
- **分页异常处理**：接入页故障入口、CR3 调度切换、VBE/e1000 MMIO 动态映射，以及退出后的页目录与物理槽回收。
- **规范化身份**：应用路径统一为规范叶名称并以 inode 识别现有实例，避免 `terminal.tsk` 与 `system/terminal.tsk` 被错误视为不同程序。

### 窗口稳定性

- **生命周期保护**：窗口新增 `generation`、`ref_count`、`closing`，合成器和任务栏改用引用快照，消除窗口关闭与绘制/点击并发导致的悬空指针。
- **窗口内存池**：固定的 8 个全屏缓冲槽改为 4 MiB 连续页分配器，按窗口实际尺寸分配，并保持最多 20 个图层。
- **临界区缩短**：窗口缓冲初始化在发布前且开启中断，层级、销毁和引用更新仅使用短 IRQ 临界区。

### 渲染与调度性能

- **脏矩形合成**：新增合并、裁剪和满队列退化策略，只重建受损逻辑区域并局部提交 framebuffer。
- **批量 RGB 绘制**：新增 `SYS_BLIT_RGB`；JPEG 预览由逐像素系统调用改为一次批量缩放 blit。
- **公平调度**：修复链表头偏置，使用优先级 + 同级 Round-Robin，并允许更高优先级任务提前抢占。
- **空闲休眠**：PID 0 静止时执行 `sti; hlt`，不再持续忙轮询。
- **性能统计**：新增帧数、全屏重绘、受损像素、framebuffer 写入像素和 idle halt 计数，终端 `sysinfo` 可查看。

### 文档

- **README 导航**：增加更新日志、TLX、TSK 开发指南和工程结构入口。
- **TSK 开发指南**：新增最小应用、窗口事件、RGB blit、TLX、链接槽、Makefile/mkfs、开始菜单、多实例和调试说明。

### 验证

- **核心回归**：新增脏矩形、连续槽分配、公平调度、最近邻缩放、分页地址计算和路径身份测试。
- **多窗口压力测试**：QEMU 中验证 6 个 Terminal 同时运行、退出一个后重新创建；页故障、坏上下文和异常标记均为 0。
- **资源变化**：6 个 `240x170` Terminal 缓冲由固定全屏槽的 1,536,000 字节降至页对齐后的 983,040 字节，减少约 36%。


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
| 38 | `SYS_LAUNCH_TSK_EX` | 按标志聚焦已有实例或创建新实例 |
| 39 | `SYS_BLIT_RGB` | 批量 RGB 图像缩放绘制 |
| 40 | `SYS_GET_VIDEO_STATS` | 获取合成器与空闲统计 |

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
