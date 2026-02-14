#ifndef DISK_H
#define DISK_H

#include "utils.h"

// 读写硬盘的基本单位是扇区 (512字节)
#define SECTOR_SIZE 512

void disk_init();
// 从 LBA 地址读取 count 个扇区到 buffer
void disk_read_sectors(int lba, int count, void* buffer);
// 写入扇区 (暂时先不实现，只做只读文件系统)
// void disk_write_sectors(int lba, int count, void* buffer);
//outb 和 inb 的封装
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a" (val), "Nd" (port));
}
#endif