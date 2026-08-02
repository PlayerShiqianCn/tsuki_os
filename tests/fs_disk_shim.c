#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define FS_OFFSET (1024 * 1024)
#define BLOCK_SIZE 1024

static int disk_fd = -1;

int test_disk_open(const char* path) {
    disk_fd = open(path, O_RDWR);
    return disk_fd >= 0;
}

void test_disk_close(void) {
    if (disk_fd >= 0) close(disk_fd);
    disk_fd = -1;
}

void disk_init(void) {}

void disk_read_sectors(int lba, int count, void* buffer) {
    size_t bytes = (size_t)count * 512u;
    off_t offset = (off_t)lba * 512;
    if (pread(disk_fd, buffer, bytes, offset) != (ssize_t)bytes) abort();
}

void disk_write_sectors(int lba, int count, const void* buffer) {
    size_t bytes = (size_t)count * 512u;
    off_t offset = (off_t)lba * 512;
    if (pwrite(disk_fd, buffer, bytes, offset) != (ssize_t)bytes) abort();
}

static uint32_t read_u32(off_t offset) {
    unsigned char b[4];
    if (pread(disk_fd, b, sizeof(b), offset) != (ssize_t)sizeof(b)) abort();
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

unsigned int test_free_blocks(void) {
    return read_u32(FS_OFFSET + BLOCK_SIZE + 12);
}

void test_fill_block_bitmap(void) {
    unsigned char full[BLOCK_SIZE];
    uint32_t bitmap_block = read_u32(FS_OFFSET + 2 * BLOCK_SIZE);
    memset(full, 0xff, sizeof(full));
    if (pwrite(disk_fd, full, sizeof(full), FS_OFFSET + (off_t)bitmap_block * BLOCK_SIZE) != (ssize_t)sizeof(full)) abort();
}

void test_corrupt_root_rec_len(unsigned short rec_len) {
    unsigned char value[2];
    uint32_t inode_table = read_u32(FS_OFFSET + 2 * BLOCK_SIZE + 8);
    off_t root_inode = FS_OFFSET + (off_t)inode_table * BLOCK_SIZE + 128;
    uint32_t root_data_block = read_u32(root_inode + 40);
    off_t field = FS_OFFSET + (off_t)root_data_block * BLOCK_SIZE + 4;
    value[0] = (unsigned char)(rec_len & 0xff);
    value[1] = (unsigned char)(rec_len >> 8);
    if (pwrite(disk_fd, value, sizeof(value), field) != (ssize_t)sizeof(value)) abort();
}

void klog_init(void) {}
void klog_write(const char* msg) { (void)msg; }
void klog_write_pair(const char* prefix, const char* value) { (void)prefix; (void)value; }
void kpanic(const char* msg) { (void)msg; abort(); }
void kpanic_exception(void) { abort(); }
int kpanic_is_active(void) { return 0; }
