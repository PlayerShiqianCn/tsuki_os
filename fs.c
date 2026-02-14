// fs.c
#include "fs.h"
#include "disk.h"
#include "utils.h"
#include "heap.h"

static Ext2SuperBlock ext2_sb;
static Ext2GroupDesc ext2_gd;
static int fs_ready = 0;

// 定义用户程序加载到的固定物理内存地址 (4MB处)
#define APP_LOAD_ADDRESS  0x300000

// 初始化文件系统
void fs_init() {
    // 读取 Superblock (Block 1)
    // Block 1 = 1024 bytes offset = 2 sectors.
    // 所以绝对扇区 = FS_BASE_SECTOR + 2
    disk_read_sectors(FS_BASE_SECTOR + 2, 2, &ext2_sb);

    // 恢复 Magic Check，因为 mkfs 修复了，现在应该能通过了
    if (ext2_sb.s_magic != EXT2_MAGIC) {
        // 如果这里失败，说明 offset 还是不对，或者数据没写进去
        fs_ready = 0;
        return;
    }

    // 读取 Block Group Descriptor (Block 2)
    // 绝对扇区 = FS_BASE_SECTOR + 4
    disk_read_sectors(FS_BASE_SECTOR + 4, 2, &ext2_gd);
    fs_ready = 1;
}

// 内部：读取 inode
static int read_inode(unsigned int inode_num, Ext2Inode* inode) {
    if (inode_num < 1 || !fs_ready) return 0;

    unsigned int inode_table_block = ext2_gd.bg_inode_table;
    unsigned int offset = (inode_num - 1) * 128;
    unsigned int block_relative = inode_table_block + (offset / 1024);
    unsigned int offset_in_block = offset % 1024;

    // 转换为绝对扇区：Base + (Block * 2)
    unsigned int sector = FS_BASE_SECTOR + (block_relative * 2);

    // 从堆分配
    char* buf = (char*)malloc(1024);
    if (!buf) return 0;
    disk_read_sectors(sector, 2, buf);

    unsigned char* src = (unsigned char*)buf + offset_in_block;
    unsigned char* dest = (unsigned char*)inode;
    for(int i=0; i<sizeof(Ext2Inode); i++) dest[i] = src[i];

    free(buf); // 释放
    return 1;
}

// 读取数据块 (辅助)
static void read_block(unsigned int block_num, void* buffer) {
    if (!buffer) return; // 防止传入NULL
    disk_read_sectors(FS_BASE_SECTOR + (block_num * 2), 2, buffer);
}

// 内部：在目录中查找文件名
static int find_file_in_dir(const char* filename, unsigned int dir_inode_num) {
    Ext2Inode dir_inode;
    if (!read_inode(dir_inode_num, &dir_inode)) return 0;

    if ((dir_inode.i_mode & 0xF000) != 0x4000) return 0;

    // 从堆分配
    char* block_buf = (char*)malloc(1024);
    if (!block_buf) return 0;
    // 读取目录的第一个块
    read_block(dir_inode.i_block[0], block_buf);
    
    unsigned int pos = 0;
    while (pos < 1024) {
        Ext2DirEntry* entry = (Ext2DirEntry*)(block_buf + pos);
        if (entry->rec_len == 0) break; // 防止死循环

        // 提取文件名用于比较
        char name[256];
        int name_len = entry->name_len;
        for(int i=0; i<name_len; i++) name[i] = entry->name[i];
        name[name_len] = '\0';
        
        if (strcmp(name, filename) == 0) {
            return entry->inode;
        }
        
        pos += entry->rec_len;
    }

    free(block_buf); // 释放
    return 0;
}

// 打开系统文件 (获取 inode 信息)
int sys_file_open(const char* filename, SystemFile* out_file) {
    if (!fs_ready) return 0;
    
    unsigned int inode_num = find_file_in_dir(filename, EXT2_ROOT_INODE);
    if (!inode_num) return 0;
    
    Ext2Inode inode;
    read_inode(inode_num, &inode);
    
    int fn_len = strlen(filename);
    if(fn_len > FS_FILENAME_LEN - 1) fn_len = FS_FILENAME_LEN - 1;

    for(int i=0; i<fn_len; i++) out_file->filename[i] = filename[i];
    out_file->filename[fn_len] = '\0';
    
    out_file->size = inode.i_size;
    out_file->inode_num = inode_num;
    out_file->type = (inode.i_mode & 0xF000) == 0x4000 ? 1 : 0;
    
    return 1;
}

// 打开应用层文件
int app_file_open(const char* filename, AppFile* out_file) {
    SystemFile sys_file;
    if (sys_file_open(filename, &sys_file)) {
        out_file->sys_file = sys_file;
        out_file->current_pos = 0;
        return 1;
    }
    return 0;
}
// 优化建议：fs_read_file 增加简单的间接块支持检测
int fs_read_file(const char* filename, void* buffer) {
    SystemFile file;
    if (!sys_file_open(filename, &file)) return 0;
    
    Ext2Inode inode;
    read_inode(file.inode_num, &inode);
    
    unsigned int bytes_read = 0;
    unsigned int size = inode.i_size;
    
    // 【警告】如果文件超过 12KB，这里会截断
    if (size > 12 * 1024) {
        // 可以在这里打印警告: "File too large, truncated!"
        size = 12 * 1024; 
    }

    char* block_buf = (char*)malloc(1024);
    if (!block_buf) return 0;

    for (int i = 0; i < 12 && bytes_read < size; i++) {
        unsigned int block_num = inode.i_block[i];
        if (block_num == 0) break;

        read_block(block_num, block_buf);
        
        unsigned int to_copy = size - bytes_read;
        if (to_copy > 1024) to_copy = 1024;
        
        // 安全内存复制
        unsigned char* d = (unsigned char*)buffer + bytes_read;
        unsigned char* s = (unsigned char*)block_buf;
        for(unsigned int j=0; j<to_copy; j++) d[j] = s[j];
        
        bytes_read += to_copy;
    }

    free(block_buf);
    return bytes_read;
}

// App 读取接口 (支持流式读取)
int app_file_read(AppFile* file, void* buffer, unsigned int size) {
    if (!fs_ready || !file) return 0;
    
    unsigned int bytes_to_read = size;
    // 防止读越界
    if (file->current_pos + size > file->sys_file.size) {
        bytes_to_read = file->sys_file.size - file->current_pos;
    }
    
    if (bytes_to_read == 0) return 0;
    
    // 简单实现：每次都把整个文件读进来，然后截取 (效率低但逻辑简单)
    // 优化建议：缓存 inode 里的 block 信息，只读需要的 block
    void* full_buf = malloc(file->sys_file.size);
    if (!full_buf) return 0;
    
    fs_read_file(file->sys_file.filename, full_buf);
    
    unsigned char* src = (unsigned char*)full_buf + file->current_pos;
    unsigned char* dest = (unsigned char*)buffer;
    for(unsigned int i=0; i<bytes_to_read; i++) dest[i] = src[i];
    
    file->current_pos += bytes_to_read;
    free(full_buf);
    
    return bytes_to_read;
}

int tsk_load(const char* filename, void** out_entry) {
    AppFile file;
    // 1. 打开文件
    if (!app_file_open(filename, &file)) {
        // console_write("Open failed\n");
        return 0;
    }
    
    // 2. 获取文件大小
    unsigned int size = file.sys_file.size;
    if (size == 0) return 0;
    
    // 3. 准备加载地址
    void* load_addr = (void*)APP_LOAD_ADDRESS;
    
    // 4. 【关键】直接读取整个文件到内存，不做任何 Header 解析
    // 因为 Makefile 里生成的已经是纯指令代码了
    if (app_file_read(&file, load_addr, size) != size) {
        // console_write("Read failed\n");
        return 0;
    }
    
    // 5. 设置入口点
    // 对于纯二进制文件，入口点就是文件的第一个字节
    *out_entry = load_addr;
    
    return 1;
}

// 修复后的 fs_list_files
void fs_list_files() {
    if (!fs_ready) return;
    Ext2Inode root_inode;
    read_inode(EXT2_ROOT_INODE, &root_inode);

    // 必须从堆分配，防止栈溢出
    char* block_buf = (char*)malloc(1024);
    if (!block_buf) return;

    // 【修正】使用 read_block 以包含 FS_BASE_SECTOR 偏移
    read_block(root_inode.i_block[0], block_buf);

    unsigned int pos = 0;
    while (pos < 1024) {
        Ext2DirEntry* entry = (Ext2DirEntry*)(block_buf + pos);
        if (entry->rec_len == 0) break; // 防止死循环
        
        char name[256];
        int l = entry->name_len;
        if (l > 255) l = 255; // 安全截断

        for(int i=0; i<l; i++) name[i] = entry->name[i];
        name[l] = '\0';
        
        // 假设你有 kprintf
        // kprintf("File: %s (inode %d)\n", name, entry->inode);
        
        pos += entry->rec_len;
    }
    free(block_buf);
}


// 【修复】获取真实的文件列表
void fs_get_file_list(char* buffer, int max_len) {
    if (!fs_ready) {
        strcpy(buffer, "FS Error");
        return;
    }

    Ext2Inode root_inode;
    read_inode(EXT2_ROOT_INODE, &root_inode);

    // 从堆分配
    char* block_buf = (char*)malloc(1024);
    if (!block_buf) {
        strcpy(buffer, "Heap Error");
        return;
    }
    read_block(root_inode.i_block[0], block_buf);
    
    unsigned int pos = 0;
    int buf_idx = 0;
    buffer[0] = '\0';

    while (pos < 1024) {
        Ext2DirEntry* entry = (Ext2DirEntry*)(block_buf + pos);
        if (entry->rec_len == 0) break;
        
        char name[256];
        int l = entry->name_len;
        for(int i=0; i<l; i++) name[i] = entry->name[i];
        name[l] = '\0';
        
        // 跳过 . 和 ..
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            int needed = l + 1; // name + \n
            if (buf_idx + needed < max_len) {
                for(int i=0; i<l; i++) buffer[buf_idx++] = name[i];
                buffer[buf_idx++] = '\n';
            }
        }
        
        pos += entry->rec_len;
    }
    buffer[buf_idx] = '\0';
    free(block_buf); // 释放
}