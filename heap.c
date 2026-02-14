#include "heap.h"

// 堆的头指针
HeapBlock* head = (HeapBlock*)HEAP_START_ADDR;

void heap_init() {
    // 初始化时，整个堆就是一个巨大的空闲块
    head->size = HEAP_INITIAL_SIZE - sizeof(HeapBlock);
    head->is_free = 1;
    head->next = 0;
}

void* malloc(unsigned int size) {
    HeapBlock* curr = head;
    
    // 简单的 4字节对齐
    unsigned int aligned_size = size;
    if (aligned_size % 4 != 0) {
        aligned_size += 4 - (aligned_size % 4);
    }

    // 遍历链表寻找合适的空闲块
    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            // 找到了！
            
            // 如果剩余空间足够大，就切割这个块 (Split)
            if (curr->size > aligned_size + sizeof(HeapBlock) + 4) {
                HeapBlock* new_block = (HeapBlock*)((char*)curr + sizeof(HeapBlock) + aligned_size);
                
                new_block->size = curr->size - aligned_size - sizeof(HeapBlock);
                new_block->is_free = 1;
                new_block->next = curr->next;
                
                curr->size = aligned_size;
                curr->next = new_block;
            }
            
            curr->is_free = 0;
            // 返回数据区的指针 (跳过头结构)
            return (void*)((char*)curr + sizeof(HeapBlock));
        }
        curr = curr->next;
    }
    return 0; // OOM (Out Of Memory)
}

void free(void* ptr) {
    if (!ptr) return;
    
    // 根据数据指针反推回头部指针
    HeapBlock* block = (HeapBlock*)((char*)ptr - sizeof(HeapBlock));
    block->is_free = 1;
    
    // 合并相邻空闲块 (Coalescing)
    // 遍历整个链表，合并所有相邻的空闲块
    HeapBlock* curr = head;
    
    while (curr) {
        // 检查当前块是否与下一个块可以合并
        if (curr->is_free && curr->next && curr->next->is_free) {
            // 检查是否相邻 (当前块的数据区末尾紧跟着下一个块)
            if ((char*)curr + sizeof(HeapBlock) + curr->size == (char*)curr->next) {
                // 合并当前块和下一个块
                curr->size += sizeof(HeapBlock) + curr->next->size;
                curr->next = curr->next->next;
            }
        }
        curr = curr->next;
    }
}