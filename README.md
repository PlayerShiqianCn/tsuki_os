# Project Tsuki OS

## 系统要求

- Linux/Unix 环境
- `nasm`
- `gcc`、`ld`，或可用的 `i686-linux-gnu-` 交叉工具链
- `qemu-system-i386`

如果系统中存在 `i686-linux-gnu-gcc`，Makefile 会自动使用对应的交叉工具链；也可以手动指定：

```bash
make CROSS=i686-linux-gnu-
```

## 构建和运行

```bash
# 编译并生成 OS 镜像
make

# 在 QEMU 中运行
make run

# 在终端显示 COM1 串口日志
make debug

# 清理构建产物
make clean
```

构建完成后，主要产物位于 `build/`：

- `build/boot.bin`：启动扇区
- `build/kernel.bin`：内核二进制
- `build/kernel.elf`：内核 ELF 文件
- `build/*.tsk`：应用程序任务文件
- `build/os-image.img`：完整 OS 镜像
- `build/qemu.log`：运行时中断和异常日志
