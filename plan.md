# Project Tsuki — 计划

## Plan 1 — UAC 提权确认弹窗（待实施）

> 关联：`kernel/syscall.c`（沙箱门禁）、`kernel/tlx.c`（TLX 门禁）、`include/syscall.h`（SandboxLevel）

### 背景

当前沙箱是纯自申报：`SYS_SET_SANDBOX`（5号调用）对 `current_process->sandbox_level`
做无条件赋值，且 `SYS_SET_SANDBOX` 在 `SANDBOX_STRICT` 白名单里——任何应用
随时可以 `set_sandbox(0)` 把自己放出来，无声无息。

本计划把"静默改等级"改成 Windows UAC 语义：**提权必须经用户点击确认**。

### 语义定义

| 请求 | 行为 |
|---|---|
| 降权（等级数字变大，自收紧，如 0→1、1→2） | 立即生效，不弹窗 |
| 提权（等级数字变小，如 1→0、2→1） | 进程阻塞，弹出 UAC 对话框 |
| 用户点"允许" | 等级变更生效，`set_sandbox` 返回 0 |
| 用户点"拒绝" / 按 Esc | 等级不变，`set_sandbox` 返回 -1 |

- `set_sandbox` 签名从 `void` 改为 `int set_sandbox(int level)`，应用可感知拒绝。
- 弹窗只由**提权请求**触发；被沙箱拒绝的普通操作（如 BASIC 下写文件）照旧
  返回错误码，不弹窗——UAC 管提权，不管报错。
- `is_syscall_allowed()` 中 `SYS_SET_SANDBOX` 保持在 STRICT 白名单内，但其
  语义已从"赋值"变为"发起请求"，白名单保留是安全的。

### 弹窗设计

- **归属**：内核所有的系统级模态覆盖层，不属于任何应用窗口，始终置顶。
- **布局**：全屏先压一层半透明暗化遮罩（背景静止冻结，Windows 式），居中
  对话框（宽度约当前分辨率 1/3，高度自适应内容）。
- **对话框内容**：
  - 标题栏：琥珀色，"提权请求"
  - 主体：进程名（`proc->name`）、PID、`当前等级 → 请求等级`
    （显示为 `BASIC → NONE` 这类人读名称）
  - 底部两个按钮：`[ 允许 ]` `[ 拒绝 ]`
- **输入**：
  - 鼠标点击按钮生效；点击对话框外无效果
  - 键盘：Enter = 允许，Esc = 拒绝
- **实现要点**：弹窗在系统调用处理内部同步跑模态循环（对齐
  `tlx_wait_for_wakeup()` 的 `sti; hlt; cli` 等待模式）。循环内：
  1. 进入时绘制暗化遮罩 + 对话框到前台缓冲；
  2. 每轮自采样 PS2 鼠标/键盘，移动光标、重绘对话框区域；
  3. 命中按钮或按键后做出决策，强制全屏重绘恢复正常桌面，然后返回。
- 请求进程置 `PROCESS_BLOCKED`，决策后恢复就绪。

### 改动清单

1. **`kernel/uac.c` + `include/uac.h`（新增）**
   - `int uac_request_elevation(Process* proc, int requested_level);`
     返回 1=允许 / 0=拒绝。暗化、对话框、模态循环、命中测试全在这里。
   - 复用现有内核绘制（`draw_rect`、`draw_text`、video 系列）和 PS2 输入。
2. **`kernel/syscall.c`**
   - `SYS_SET_SANDBOX` 分支：若 `regs->ebx < current_process->sandbox_level`
     （提权）→ 调 `uac_request_elevation()`，允许才赋值，拒绝时 `eax = -1`；
     否则（自收紧或已是 NONE）直接赋值，`eax = 0`。
   - `is_syscall_allowed()` 不改（见语义说明）。
3. **`userspace/lib.c` + `include/lib.h`**
   - `set_sandbox` 返回 `int`，透传 syscall 返回值。
4. **`include/syscall.h`**
   - 无新调用号；可加对话框尺寸/颜色常量。
5. **`Makefile`**
   - KERNEL_OBJS 增加 `build/uac.o`。
6. **`kernel/tlx.c`**
   - 无改动。TLX 没有设置沙箱的操作码，提权唯一入口是 `SYS_SET_SANDBOX`。
7. **应用层**
   - 现有 `set_sandbox(1)` 调用（terminal/image/wm/start/app）不触发弹窗，
     无需改动。
   - `settings_tsk` 的 `set_sandbox(0)` 将首次触发弹窗——这是预期行为。
   - 终端后续可把需要特权的命令接到 `set_sandbox(0)` 上做每命令提权
     （与现有 `sudo_mode` 思路衔接），本计划不实现。

### 验收标准

- [ ] BASIC 应用调 `set_sandbox(0)`：屏幕暗化、弹窗居中，显示正确进程名/PID/等级。
- [ ] 点"拒绝"或 Esc：返回 -1，等级仍为 BASIC，后续写文件仍被拒绝。
- [ ] 点"允许"或 Enter：返回 0，等级变为 NONE，写文件成功。
- [ ] STRICT 应用 `set_sandbox(0)`：同样弹窗（白名单语义已变为"请求"）。
- [ ] 自收紧（如 NONE→BASIC）：不弹窗，立即生效。
- [ ] 弹窗期间点击对话框外无效，背景不刷新、不响应其他窗口。
- [ ] 决策后桌面恢复正常重绘，无残影。
- [ ] QEMU 冒烟：启动桌面 → 打开 demo app → 手动触发一次允许、一次拒绝，
      全程无页故障。

### 明确不做（本计划外）

- 子进程继承 `sandbox_level`（当前 `new_proc->sandbox_level = 0`，提权批准后
  spawn 的子进程仍是全权——这是下一个要堵的洞）。
- `TLX_OP_SPAWN` 的 UAC 化。
- 凭据/管理员口令：当前点击即过，等价于 Windows UAC 的无密码档；口令校验
  需要先有用户与凭据存储设计。

---

## Plan 2 — samain 包管理系统（待实施，依赖 Plan 1）

> 关联：Plan 1（UAC）、`kernel/tlx.c`（TLX 文件接口）、`apps/terminal_tsk.c`、`net_http_get`（网络获取，Phase 2）

### 背景与定位

应用目前以裸 `.tsk` 文件的形式存在于文件系统，装机靠手动拷贝，卸载靠记忆。
samain 是 Tsuki 的包管理器：安装/卸载/查询 `.sama` 格式软件包，维护包数据库，
注册开始菜单磁贴。定位对标 dpkg/apt 的关系——`.sama` 是包文件，samain 是
操作它的工具；仓库索引和在线解析属于 Phase 2。

### .sama 包格式 v1

二进制容器，小端，**不压缩**（内核侧和 samain 都保持零依赖解析）。约束来自
现有 TLX 边界：单文件 ≤ `TLX_MAX_FILE_BYTES`（0x44000），路径 ≤ 63 字符
（`TLX_MAX_PATH`）。

```
Header（定长 0x20）
  0x00  u32  magic = 'SAMA'（0x414D4153）
  0x04  u16  format version = 1
  0x06  u16  header size = 0x20
  0x08  u32  file count
  0x0C  u32  metadata 块大小（字节）
  0x10  u32  全 payload CRC32（所有文件数据按序计算）
  0x14  u32  保留（0）

Metadata 文本块（UTF-8，key=value 每行一条，# 开头为注释）
  name=samain            # 必填，包名，[a-z0-9-]
  version=0.1.0          # 必填
  arch=i386              # 必填
  author=...             # 可选
  description=...        # 可选
  depends=foo,bar        # 可选，逗号分隔已装包名
  tile=标题|文件名|颜色   # 可选，安装后注册开始菜单磁贴

File entry（逐个排列，数量 = file count）
  u16  path 长度
  char path[len]        # 安装目标绝对路径，如 /apps/hello.tsk
  u32  data size
  u32  data CRC32
  byte data[size]
```

- 安装时逐条校验：magic、版本、每文件 CRC、整体 payload CRC；任一不符即
  拒绝安装，**不落盘任何半成品**。
- 格式无安装脚本、无钩子——安装动作是纯声明式的"复制文件 + 写数据库 +
  注册磁贴"。可执行逻辑只来自包内的 `.tsk` 本身，沙箱在它自己的启动入口
  申报。这是有意的安全取舍。

### 包数据库与目录约定

```
/pkg/installed.list        # 已装包总表，每行: name|version
/pkg/<name>.manifest       # 每包清单：版本、依赖、安装的全部文件路径
/apps/                     # .tsk 的标准安装位置（约定，非强制）
```

- `installed.list` 是依赖判定的唯一事实源：`depends=` 中每个名字必须已
  在表中，否则拒绝安装并提示缺失项。
- 卸载前反查：若有其他已装包的 manifest 依赖它，拒绝卸载。
- 卸载按 manifest 列表逐个 `tlx_unlink`，删 manifest，再更新
  `installed.list`。运行中的实例不受影响（内核私有物理镜像槽保证，
  见 README 多实例机制）。

### samain.tsk（应用本体）

独立 TSK 应用，自带窗口 + 输入行 + 输出区（复用终端的绘制模式），子命令：

```
install <路径.sama>   校验 → 提权 → 落盘 → 写库 → 注册磁贴
remove <name>        反查依赖 → 提权 → 逐文件删除 → 更新库
list                 列出 installed.list
info <name|文件>     显示 manifest 元数据
```

### 沙箱与 UAC 集成（依赖 Plan 1）

samain 的权限流是 Plan 1 的标准用例：

1. 启动时 `set_sandbox(1)`（自收紧，不弹窗）。
2. 用户执行 `install`/`remove` 时 `set_sandbox(0)` → **触发 UAC 弹窗**。
3. 拒绝：命令失败，提示"未获授权"；允许：执行写操作，完成后回到 BASIC。

即**按操作提权**，不是启动提权——用户能在看到具体要装什么之后再决定。
Plan 1 未实施前，samain 的提权会静默成功（现状语义），功能不受阻但 UX
降级为无声。

### 工具链

- **`tools/sama_pack.py`（新增，主机侧）**：输入一个包描述文件 + 文件列表，
  输出 `.sama`。负责计算 CRC32、生成 metadata 块。仓库已有 `mkpng.py`
  先例，Python 是现成的主机工具链。
- **内核侧零改动**：解析、校验、落盘全部走现有 TLX 文件接口
  （`tlx_open/read/seek/write/unlink/rename`），不需要新 syscall。

### 实施阶段

- **Phase 1（MVP）**：格式 v1 + `sama_pack.py` + `samain.tsk` 四个子命令 +
  `/pkg` 数据库 + CRC 校验 + UAC 按操作提权 + 磁贴注册。
- **Phase 2**：`/pkg/repo.list`（URL 列表）→ `sama update` 拉索引、
  `sama install <name>` 经 `net_http_get` 下载到缓存目录后走 Phase 1 安装流。
- **Phase 3**：upgrade（版本比较）、卸载保留 `.bak` 回滚、依赖自动排序安装。

### 验收标准（Phase 1）

- [ ] `sama_pack.py` 打出一个含 hello.tsk + 数据文件的 demo 包，samain 能识别。
- [ ] `install`：UAC 弹窗一次 → 允许后文件落盘、磁贴出现、`installed.list` 更新。
- [ ] 拒绝提权：安装中止，磁盘与数据库无任何变更。
- [ ] 人为翻转包内 1 字节：CRC 校验失败，拒绝安装。
- [ ] `depends` 指向未安装的包：拒绝安装，提示缺失名。
- [ ] 被依赖的包拒绝卸载；无依赖包卸载后文件、磁贴、库记录全部消失。
- [ ] `list` / `info` 输出与库一致。
- [ ] QEMU 冒烟：装 → 运行 → 卸 → 重装，全程无页故障。

### 明确不做（本计划外）

- 压缩（v1 原样存储；需要时加 RLE/LZSS 升 format version）。
- 安装脚本/钩子（见上，声明式是有意为之）。
- 依赖解析器与自动排序（Phase 3 才考虑，v1 只做判定与拒绝）。
- TSK 启动参数（argv）——终端 `sama xxx` 若要转发参数需要先给
  `launch_tsk` 加参数通道，本计划不依赖它。
- 数字签名（需要先有哈希/非对称原语，Phase 3 之后的独立计划）。

---

## Plan 3 — 工具链自举地基（待实施，依赖序：M0 → M1 → M2）

> 关联：`include/tlx.h`（TLX ABI）、`kernel/tlx.c`、`kernel/process.c`（栈布局）、
> `kernel/heap.c`（仅内核堆）、`tools/make_tsk.c`（TSK2 打包）

### 背景与结论

目标：在 Tsuki OS 上原生编译程序（自举）。结论先行——

- **原生 gcc 不做**：需要完整 libc、fork/exec 编排 cc1/as/ld、binutils 的 bfd、
  数十 MB 磁盘与 64MB 级内存，按年计的工程，且主机交叉编译已是完整工作流。
- **现实路线是 nasm → tcc**：nasm 自包含（~150k 行，仅依赖 stdio/malloc），
  `nasm -f bin` 平镜像 + TSK2 包装即可产出可运行 .tsk，全程绕开链接器；
  tcc 单二进制自带汇编器与链接器，不需要 fork，是公认的小型自举路径。

### 现状硬卡点（已核实）

| 缺口 | 现状 | 需要 |
|---|---|---|
| 用户态堆 | 无（`kernel/heap.c` 的 malloc 是 ring 0 内核堆，未暴露） | `TLX_OP_BRK` + 用户堆区 |
| 栈大小 | `STACK_SIZE = 12288`（12KB，`kernel/process.c`） | 工具类 ≥1MB，可配置 |
| argv | `TLX_OP_SPAWN` 只收路径 | argv 数组 + 退出码 |
| 单文件上限 | `TLX_MAX_FILE_BYTES = 0x44000`（270KB） | nasm 自身 ~800KB，必须放开 |
| 路径上限 | `TLX_MAX_PATH = 64` | ≥256 |
| 进程数 | `TLX_CONTEXT_COUNT = 16` | tcc/nasm 单进程可容忍 |
| 挂钟 | 仅 ticks | RTC 读数（`__DATE__`/make 时间戳） |
| 周边 | 无文本编辑器、终端无重定向 | 自举闭环缺编辑器一环 |

### M0 — 公共地基

1. `TLX_OP_BRK`：进程页空间划一块可增长堆区（`paging_create_process_space`
   已给每进程独立页目录，加 region 顺手），lib 配简单 `tlx_malloc`。
2. 栈配置化：TSK2 header 的 `flags` 字段加"大栈"位，置位进程栈 1MB。
3. `TLX_OP_SPAWN` 扩展 argv 指针数组 + 等待子进程退出码。
4. `TLX_MAX_FILE_BYTES` 改动态或提至 4MB；`TLX_MAX_PATH` 提至 256。
5. 磁盘镜像加大，启动建 `/tmp`；RTC 日期时间 syscall。

### M1 — nasm 自举（1-2 周量级）

- 写 `tsukilib`（1-2k 行）：stdio 压到 TLX 上，malloc 压到 BRK 上。
- 移植 nasm，`-f bin` 输出平镜像。
- 目标机写 `tskwrap`（`make_tsk.c` 的目标机版，~20 行）：平镜像套 TSK2 头。
- 产出：在 Tsuki 上 `nasm hello.asm` → `tskwrap` → 可运行的 hello.tsk。

### M2 — tcc 自举（数周量级）

- 扩充 tsukilib（realloc、更多 stdio），移植 tcc i386 后端。
- 教 tcc 吐平二进制（或写目标机 elf2tsk）。
- 产出：Tsuki 上编译 C。tsukilib + tskwrap + tcc 打成 `.sama`，
  作为 Plan 2 的第一个真实客户。

### 验收标准

- [ ] M0：用户态程序 malloc/free 稳定工作；大栈 TSK 递归 >12KB 不崩；
      spawn 携 argv 且父进程拿到退出码；>270KB 文件可创建可读。
- [ ] M1：tsk_girl 同款流程——目标机汇编出 .tsk 并成功运行；
      tsukilib 主机/目标机双跑测试。
- [ ] M2：目标机 tcc 编译 hello.c 直接运行；tcc 能编译 tsukilib 自身（自举闭环）。

### 明确不做（本计划外）

- 原生 gcc / binutils（理由见背景）。
- 管道与 shell 重定向（make 级编排推到 M2 之后）。
- 多用户/权限（工具链在单用户假设下运行）。

---

## 候补池（Backlog，未排期）

> 从 PROJECT_STRUCTURE.md「后续方向」迁入。未设计、未排期；立项时升级为
> 正式 Plan 并补齐改动清单与验收标准。编号不代表优先级。

1. **`..` 中间级回退与 chdir 深度跟踪** —— 当前 `resolve_path` 的 `..`
   只回退到根，终端 `cd` 同语义。需要内核侧逐级跟踪或路径规范化。
2. **多块组文件系统** —— 现单块组约 8MB 数据区上限。是 Plan 3-M0
   「加大磁盘镜像」的先决依赖：放不下 nasm/tcc 就先做它。
3. **设备驱动框架与动态加载** —— 统一注册/匹配接口。`.tso` 文件形态已在
   FS 预留（lib.tso / jpeg.tso），但内核尚无加载器。
4. **Ring 3 用户态/内核态分离** —— 当前用户代码全在 ring 0，沙箱是逻辑
   门禁；真正的权限边界要动 GDT/TSS/系统调用门。工作量最大，也是 Plan 1
   UAC 从"君子协定"变"真边界"的终局答案。
5. **窗口最小化与最大化** —— WM 层功能补全。
6. **TCP 连接复用与 HTTPS** —— 现网络栈仅 ping/DNS/简单 HTTP；HTTPS
   依赖 TLS 栈、熵源与大数运算原语。
