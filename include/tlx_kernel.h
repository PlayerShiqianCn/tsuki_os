#ifndef TLX_KERNEL_H
#define TLX_KERNEL_H

#include "syscall.h"

struct Process;

void tlx_process_init(struct Process* process);
void tlx_process_release(struct Process* process);
int tlx_dispatch(Registers* regs);

#endif
