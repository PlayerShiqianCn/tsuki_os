#ifndef HEAP_H
#define HEAP_H

#include "utils.h" // 需要 size_t 定义，如果没有，在 utils.h 里加 typedef unsigned int size_t;

// 定义堆的起始地址 (1MB 处，远离内核代码 0x1000)
#define HEAP_START_ADDR 0x100000
// 定义堆的大小 (例如 512KB)
#define HEAP_INITIAL_SIZE 0x80000

typedef struct HeapBlock {
    unsigned int size;      // 数据块大小
    int is_free;            // 是否空闲
    struct HeapBlock* next; // 下一个块
} HeapBlock;

void heap_init();
void* malloc(unsigned int size);
void free(void* ptr);

#endif