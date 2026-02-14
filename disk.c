#include "disk.h"

// ATA 端口定义 (Primary Bus)
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20

// 汇编辅助：从端口读入 count 个 word (2字节)
static inline void insw(unsigned short port, void* addr, unsigned int count) {
    __asm__ volatile ("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

void disk_init() {
    // ATA PIO 模式通常不需要复杂的初始化
}

// 等待硬盘不再繁忙 (BSY) 且准备好数据传输 (DRQ)
void disk_wait() {
    while(1) {
        unsigned char status = inb(ATA_STATUS);
        // bit 7 (BSY) == 0, bit 3 (DRQ) == 1, or bit 0 (ERR) == 1
        if ((status & 0x80) == 0 && (status & 0x08)) break;
        if (status & 0x01) return; // Error
    }
}

void disk_read_sectors(int lba, int count, void* buffer) {
    // 【防崩溃检查】: 如果 buffer 是 NULL，绝对不能读，否则覆盖 IVT (0x00) 导致黑屏
    if (buffer == 0) {
        return;
    }

    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F)); // 0xE0 = Master Drive, LBA Mode
    outb(ATA_SECTOR_CNT, (unsigned char)count);
    outb(ATA_LBA_LO, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ); // 发送“读”命令

    unsigned short* ptr = (unsigned short*)buffer;
    
    for (int i = 0; i < count; i++) {
        disk_wait(); // 等待硬盘准备好
        // 读取 256 个字 (512 字节)
        insw(ATA_DATA, ptr, 256);
        ptr += 256;
    }
}