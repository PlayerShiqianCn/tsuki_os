# 更新记录

## 2026-10-13 — v0.5.1 "Clean Splits"

### 终端重构

- **三文件拆分**：`terminal_tsk.c`（1483 行）拆为外壳（374 行）+ 命令层 `terminal_cmd.c` + 共享头 `terminal_internal.h`；跨界仅输出层五个函数、`run_command` 与两个共享状态（`current_dir`/`sudo_mode`）。
- **命令表驱动**：460 行 if 分发链替换为 `{name, handler}` 命令表（26 条），裸命令统一输出 Usage，`ip`/`gw` 共享实现；新增命令只需一个 handler + 一行表项。
- **无框输入行**：移除底部输入框与分隔线，提示符与输入以主题色直绘，新增 250ms 闪烁块状光标（仅聚焦时显示，主循环按相位翻转重绘）。

### 用户态 string.h

- 新增 `include/string.h` + `userspace/string.c`：标准 ISO 名（`strlen`/`strcmp`/`strncmp`/`memcpy`/`memmove`/`memset`/`memcmp`/`strchr` 等）+ BSD 风格有界接口 `strlcpy`/`strlcat`/`str_ends_with`。
- 终端手搓的 `s_len`/`s_cmp`/`s_ncmp`/`s_copy`/`s_append` 全部替换为标准名；全部 6 个应用的链接规则挂上 `string.o`，同时满足 freestanding 下 gcc 隐式 `memcpy`/`memset` 调用的链接需求。

### win_get_size 客户区修正

- **修复**：`SYS_WIN_GET_SIZE` 原返回窗口外框尺寸（含标题栏 28px + 边框 1px），而沙箱绘制按客户区裁剪，两者相差 29px——终端输入行整行画进被裁剪区。现返回与 `sys_draw_*_sandboxed` 公式一致的客户区尺寸（borderless-aware）。

### JPEG 解码器拆分

- 1153 行单文件拆为入口 `jpeg.c`（403 行，probe/基线解码/分发）+ 原语 `jpeg_core.c`（276 行）+ 渐进式 `jpeg_progressive.c`（452 行）+ 共享头 `jpeg_internal.h`；渐进式系数存储不再被基线路径背负。
- **像素级等价验证**：主机测试台对渐进式样张（FNV1a `6166ab83`）与基线式样张（`e9461dab`）拆前拆后校验和逐位一致。
- `jpeg.tso` 由三个目标经 `ld -r` 合并后输出，形态保持不变。

## 2026-08-18 — v0.5.0 "Deep Paths + Resize"

### 窗口缩放

- **拖拽缩放**：非无边框窗口右下角新增 12px 缩放手柄，鼠标拖拽可实时调整窗口尺寸。
- **`win_resize()` 内核实现**：重新分配页对齐像素缓冲区，正确处理行步幅变化的逐行像素复制；缩小时释放多余页面，放大时分配新页面并填充背景色。
- **`SYS_WIN_RESIZE`（46）**：应用可通过系统调用编程式调整自身窗口大小。
- **`SYS_WIN_GET_SIZE`（47）**：应用可查询当前窗口客户区尺寸，用于布局重计算。
- **终端动态布局**：终端检测窗口尺寸变化后自动重算列数、输出行数和输入区位置，不再使用编译期硬编码。
- **缩放手柄视觉指示**：窗口右下角绘制两条对角线标记，提示可缩放区域。

### 多级嵌套目录遍历

- **递归路径解析**：`fs.c` 新增 `resolve_path()`，从根 inode 逐级解析多级路径（如 `docs/api/readme.txt`），替代原有的 `split_lookup_path()` 单级限制。
- **多块目录读取**：`fs_get_file_list()` 和 `find_file_in_dir()` 改为遍历目录 inode 的所有直接块和一级间接块，不再局限于 `i_block[0]` 的 1024 字节。
- **`resolve_dir_inode()` 升级**：使用 `resolve_path()` 处理任意深度的目录路径，支持 `.`、`..` 和连续斜杠。
- **`sys_file_open()` 重构**：使用 `resolve_path()` 替代旧的 split + 单级查找模式，统一支持多级路径。

### 文件重命名与移动

- **`SYS_RENAME`（44）**：新增 `fs_rename()` 内核实现，支持同目录重命名（原地更新目录项）和跨目录重命名（移除旧条目 + 添加新条目）。
- **`SYS_MOVE`（45）**：新增 `fs_move()`，将文件从一个目录移动到另一个目录。
- **`TLX_OP_RENAME`（19）**：新增 TLX 兼容层 `tlx_rename()` 用户态接口。
- **终端 `rename` 命令**：`rename <old> <new>` 重命名文件。
- **沙箱策略**：`SYS_RENAME` 和 `SYS_MOVE` 在 `SANDBOX_BASIC` 中被禁止。

### 终端增强

- **命令历史**：终端新增 `CMD_HISTORY_MAX` 条命令历史栈，支持 ↑/↓ 箭头键浏览。
- **PS/2 箭头键**：`ps2_getchar()` 识别 0xE0 前缀的扩展扫描码，返回 `KEY_UP`/`KEY_DOWN`/`KEY_LEFT`/`KEY_RIGHT` 专用码。
- **输入草稿保存**：浏览历史时自动保存当前未提交的输入，返回后恢复。

### 系统调用扩展

| 编号 | 名称 | 功能 |
|------|------|------|
| 44 | `SYS_RENAME` | 重命名文件 |
| 45 | `SYS_MOVE` | 跨目录移动文件 |
| 46 | `SYS_WIN_RESIZE` | 调整窗口尺寸 |
| 47 | `SYS_WIN_GET_SIZE` | 查询当前窗口客户区尺寸 |

### 回归测试

- **多块目录测试**：验证超过 1024 字节目录的遍历与查找。
- **重命名测试**：同目录重命名、跨目录重命名、重名冲突。
- **路径解析测试**：多级路径、`.`/`..`、连续斜杠、尾部斜杠。
- **大文件读写回验**：> 1024 字节跨块写入、间接块写入、截断验证。
- **调度器测试**：`sched_pick_next_index()` 优先级与轮转。

### 文档

- **PROJECT_STRUCTURE.md**：更新"当前限制"和"后续方向"。
- **CHANGELOG.md**：新增 v0.5.0 条目。
- **TLX.md**：新增 `tlx_rename` 说明。

## 2026-08-17 — UI foundation（未发布）

- **圆角 RGB primitive**：新增 `SYS_DRAW_ROUNDED_RECT_RGB` 与 `draw_round_rect_rgb()`，应用可在一次系统调用中绘制圆角 24 位 RGB 区域。
- **统一暗色主题**：新增 `include/theme.h`，集中定义背景、表面、控件、边框、强调色、文本和状态色。
- **公共控件现代化**：按钮、输入框和滑块移除旧式 3D 高光/凹凸边框，改为扁平圆角样式，同时保留原有颜色参数语义。
- **窗口 chrome 更新**：窗口改用暗色标题栏、细边框、柔和阴影、焦点强调线和圆形关闭按钮；窗口生命周期、分页和命中区域保持不变。
- **严格沙箱绘图补齐**：`SYS_DRAW_RECT_RGB` 与圆角 RGB 绘图在 `SANDBOX_STRICT` 中可用。
- **原生 TrueType 字体**：新增内核 TTF 解析与光栅化模块，运行时从 `/system/ui.ttf` 读取 `cmap`、`hmtx`、`loca` 与 `glyf`，对 Basic Latin simple glyph 使用二次 Bézier 展平；小字号覆盖率会量化成偏 crisp 的三级边缘，避免 320x200 逻辑画布放大后发糊。
- **可变字宽与 RGB 文本**：`draw_text()` ABI 保持不变，同时新增 `SYS_MEASURE_TEXT` / `measure_text()` 与 `SYS_DRAW_TEXT_RGB` / `draw_text_rgb()`；窗口标题、任务栏与应用内容可统一使用 24 位主题色。
- **字体资源控制**：默认 UI 字体改为约 190 KiB 的 Droid Sans，ASCII 32–126 全部为 simple glyph；完成 glyph cache 后立即释放原始 TTF，避免长期占用 512 KiB kernel heap。
- **系统级主题统一**：桌面、任务栏、 Start、Image Viewer、Settings 与窗口 chrome 统一迁移到 `theme.h` 的暗色 surface/control/accent 体系，移除旧的纯青桌面、亮灰内容区、纯蓝侧栏和紫色 Start 背景。
- **安全回退**：构建机不存在 `UI_FONT` 或某个 glyph 不受当前 TTF 子集支持时，自动回退内置 8×8 bitmap 字体，不阻断启动。
- **1080p BGA 输出**：QEMU `stdvga`/Bochs BGA 默认物理 framebuffer 升级到 `1920x1080x32`，新增显存容量检查与寄存器写后读回验证；同时保留 `1280x720` 与旧 4:3 模式。
- **640x360 逻辑桌面**：逻辑合成画布由 `320x200` 升级到 `640x360`，在 1080p 下形成精确 3× 整数缩放；编译期检查保证 921,600 字节后缓冲不会越过 klog 保留区。
- **高分辨率布局**：Start 页面扩展到 640x360，默认应用窗口改为居中布局，Settings 增加 720p/1080p 切换项，默认配置切换到 1920x1080。

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
