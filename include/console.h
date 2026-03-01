#ifndef CONSOLE_H
#define CONSOLE_H

#include "window.h"

// 启动 tsk 应用（进程管理功能，被 syscall.c 依赖）
int console_launch_tsk(const char* filename);

#endif
