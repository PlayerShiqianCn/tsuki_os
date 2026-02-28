// fs.c
#include "fs.h"
#include "disk.h"
#include "utils.h"
#include "heap.h"
#include "klog.h"

static Ext2SuperBlock ext2_sb;
static Ext2GroupDesc ext2_gd;
static int fs_ready = 0;
static int fs_lock_depth = 0;
static unsigned int fs_lock_flags = 0;
static unsigned char fs_inode_block_buf[1024];
static unsigned char fs_dir_block_buf[1024];
static unsigned char fs_io_block_buf[1024];
static unsigned int fs_indirect_entries_buf[1024 / 4];

#define HIDDEN_SUFFIX "._hid_"

static int find_file_in_dir(const char* filename, unsigned int dir_inode_num);

static unsigned int irq_save_disable(void) {
    unsigned int flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(unsigned int flags) {
    __asm__ volatile ("push %0; popf" :: "r"(flags) : "memory", "cc");
}

static void fs_lock_enter(void) {
    if (fs_lock_depth == 0) {
        fs_lock_flags = irq_save_disable();
    }
    fs_lock_depth++;
}

static void fs_lock_leave(void) {
    if (fs_lock_depth <= 0) return;
    fs_lock_depth--;
    if (fs_lock_depth == 0) {
        irq_restore(fs_lock_flags);
    }
}

static int str_ends_with(const char* s, const char* suffix) {
    int ls = strlen(s);
    int lf = strlen(suffix);
    if (ls < lf) return 0;
    return strcmp(s + (ls - lf), suffix) == 0;
}

static void copy_trim_hidden_suffix(char* dst, const char* src, int max_len) {
    if (!dst || !src || max_len <= 0) return;

    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';

    if (str_ends_with(dst, HIDDEN_SUFFIX)) {
        int trimmed = strlen(dst) - strlen(HIDDEN_SUFFIX);
        if (trimmed >= 0) dst[trimmed] = '\0';
    }
}

static const char* path_leaf(const char* path) {
    const char* leaf = path;
    if (!path) return "";
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') leaf = path + i + 1;
    }
    return leaf;
}

static int split_lookup_path(const char* path, char* dir, int dir_max, char* name, int name_max) {
    const char* slash;
    const char* rest;
    int dir_len;
    int name_len;

    if (!path || !dir || !name || dir_max <= 0 || name_max <= 0) return 0;
    while (*path == '/') path++;
    if (*path == '\0') return 0;

    dir[0] = '\0';
    name[0] = '\0';

    slash = 0;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') {
            slash = path + i;
            break;
        }
    }

    if (!slash) {
        name_len = strlen(path);
        if (name_len >= name_max) name_len = name_max - 1;
        memcpy(name, path, name_len);
        name[name_len] = '\0';
        return 1;
    }

    dir_len = (int)(slash - path);
    if (dir_len <= 0 || slash[1] == '\0') return 0;
    rest = slash + 1;
    for (int i = 0; rest[i]; i++) {
        if (rest[i] == '/') return 0;
    }
    if (dir_len >= dir_max) dir_len = dir_max - 1;
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';

    name_len = strlen(rest);
    if (name_len >= name_max) name_len = name_max - 1;
    memcpy(name, rest, name_len);
    name[name_len] = '\0';
    return 1;
}

static unsigned int resolve_dir_inode(const char* dir_path) {
    char dir_name[FS_FILENAME_LEN];

    if (!dir_path || dir_path[0] == '\0') return EXT2_ROOT_INODE;
    while (*dir_path == '/') dir_path++;
    if (*dir_path == '\0') return EXT2_ROOT_INODE;
    if (strcmp(dir_path, ".") == 0) return EXT2_ROOT_INODE;

    {
        int i = 0;
        while (dir_path[i] && dir_path[i] != '/' && i < FS_FILENAME_LEN - 1) {
            dir_name[i] = dir_path[i];
            i++;
        }
        dir_name[i] = '\0';
        if (dir_path[i] != '\0') return 0;
    }

    return find_file_in_dir(dir_name, EXT2_ROOT_INODE);
}

static void* resolve_app_load_address(const char* filename) {
    char base_name[FS_FILENAME_LEN];
    const char* leaf;
    if (!filename) return 0;
    copy_trim_hidden_suffix(base_name, filename, sizeof(base_name));
    leaf = path_leaf(base_name);

    if (strcmp(leaf, "app.tsk") == 0) return (void*)APP_LOAD_APP;
    if (strcmp(leaf, "terminal.tsk") == 0) return (void*)APP_LOAD_TERMINAL;
    if (strcmp(leaf, "wm.tsk") == 0) return (void*)APP_LOAD_WM;
    if (strcmp(leaf, "start.tsk") == 0) return (void*)APP_LOAD_START;
    if (strcmp(leaf, "image.tsk") == 0) return (void*)APP_LOAD_IMAGE;
    if (strcmp(leaf, "settings.tsk") == 0) return (void*)APP_LOAD_SETTINGS;

    // 目前 flat binary 不是位置无关代码，未知 .tsk 无法安全并行加载
    return 0;
}

// 初始化文件系统
void fs_init() {
    // 按磁盘块读取到临时 1KB 缓冲，避免直接读入结构体导致越界覆盖全局状态
    unsigned char sb_raw[1024];
    unsigned char gd_raw[1024];

    // 读取 Superblock (Block 1)
    // Block 1 = 1024 bytes offset = 2 sectors.
    // 所以绝对扇区 = FS_BASE_SECTOR + 2
    disk_read_sectors(FS_BASE_SECTOR + 2, 2, sb_raw);
    memset(&ext2_sb, 0, sizeof(ext2_sb));
    {
        unsigned int copy_len = sizeof(ext2_sb);
        if (copy_len > 1024) copy_len = 1024;
        memcpy(&ext2_sb, sb_raw, copy_len);
    }

    // 恢复 Magic Check，因为 mkfs 修复了，现在应该能通过了
    if (ext2_sb.s_magic != EXT2_MAGIC) {
        // 如果这里失败，说明 offset 还是不对，或者数据没写进去
        fs_ready = 0;
        return;
    }

    // 读取 Block Group Descriptor (Block 2)
    // 绝对扇区 = FS_BASE_SECTOR + 4
    disk_read_sectors(FS_BASE_SECTOR + 4, 2, gd_raw);
    memset(&ext2_gd, 0, sizeof(ext2_gd));
    {
        unsigned int copy_len = sizeof(ext2_gd);
        if (copy_len > 1024) copy_len = 1024;
        memcpy(&ext2_gd, gd_raw, copy_len);
    }
    fs_ready = 1;
}

int fs_is_ready(void) {
    return fs_ready;
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

    disk_read_sectors(sector, 2, fs_inode_block_buf);

    unsigned char* src = fs_inode_block_buf + offset_in_block;
    unsigned char* dest = (unsigned char*)inode;
    for (unsigned int i = 0; i < sizeof(Ext2Inode); i++) dest[i] = src[i];
    return 1;
}

static int write_inode(unsigned int inode_num, const Ext2Inode* inode) {
    unsigned int inode_table_block;
    unsigned int offset;
    unsigned int block_relative;
    unsigned int offset_in_block;
    unsigned int sector;
    char* buf;
    unsigned char* dst;

    if (inode_num < 1 || !fs_ready || !inode) return 0;

    inode_table_block = ext2_gd.bg_inode_table;
    offset = (inode_num - 1) * 128;
    block_relative = inode_table_block + (offset / 1024);
    offset_in_block = offset % 1024;
    sector = FS_BASE_SECTOR + (block_relative * 2);

    buf = (char*)malloc(1024);
    if (!buf) return 0;

    disk_read_sectors(sector, 2, buf);
    dst = (unsigned char*)buf + offset_in_block;
    memcpy(dst, inode, sizeof(Ext2Inode));
    disk_write_sectors(sector, 2, buf);
    free(buf);
    return 1;
}

// 读取数据块 (辅助)
static void read_block(unsigned int block_num, void* buffer) {
    if (!buffer) return; // 防止传入NULL
    disk_read_sectors(FS_BASE_SECTOR + (block_num * 2), 2, buffer);
}

static void write_block(unsigned int block_num, const void* buffer) {
    if (!buffer) return;
    disk_write_sectors(FS_BASE_SECTOR + (block_num * 2), 2, buffer);
}

static unsigned int inode_data_capacity(const Ext2Inode* inode) {
    unsigned int blocks = 0;

    if (!inode) return 0;

    for (int i = 0; i < 12; i++) {
        if (inode->i_block[i] == 0) break;
        blocks++;
    }

    if (inode->i_block[12] != 0) {
        unsigned int* indirect_entries = (unsigned int*)malloc(1024);
        if (indirect_entries) {
            read_block(inode->i_block[12], indirect_entries);
            for (int i = 0; i < (1024 / 4); i++) {
                if (indirect_entries[i] == 0) break;
                blocks++;
            }
            free(indirect_entries);
        }
    }

    return blocks * 1024;
}

// 内部：在目录中查找文件名
static int find_file_in_dir(const char* filename, unsigned int dir_inode_num) {
    Ext2Inode dir_inode;
    if (!read_inode(dir_inode_num, &dir_inode)) {
        klog_write("read inode fail");
        return 0;
    }

    if ((dir_inode.i_mode & 0xF000) != 0x4000) {
        klog_write("not a dir");
        return 0;
    }
    // 读取目录的第一个块
    read_block(dir_inode.i_block[0], fs_dir_block_buf);
    
    unsigned int pos = 0;
    while (pos < 1024) {
        Ext2DirEntry* entry = (Ext2DirEntry*)(fs_dir_block_buf + pos);
        if (entry->rec_len == 0) break; // 防止死循环

        // 提取文件名用于比较
        char name[256];
        int name_len = entry->name_len;
        for(int i=0; i<name_len; i++) name[i] = entry->name[i];
        name[name_len] = '\0';
        
        if (strcmp(name, filename) == 0) {
            unsigned int inode_num = entry->inode;
            return inode_num;
        }
        
        pos += entry->rec_len;
    }

    klog_write_pair("dir miss ", filename);
    return 0;
}

// 打开系统文件 (获取 inode 信息)
int sys_file_open(const char* filename, SystemFile* out_file) {
    char dir_name[FS_FILENAME_LEN];
    char base_name[FS_FILENAME_LEN];
    unsigned int dir_inode_num;
    unsigned int inode_num;

    fs_lock_enter();
    if (!fs_ready) {
        fs_lock_leave();
        return 0;
    }

    if (!split_lookup_path(filename, dir_name, sizeof(dir_name), base_name, sizeof(base_name))) {
        klog_write_pair("path bad ", filename);
        fs_lock_leave();
        return 0;
    }

    dir_inode_num = EXT2_ROOT_INODE;
    if (dir_name[0]) {
        dir_inode_num = find_file_in_dir(dir_name, EXT2_ROOT_INODE);
        if (!dir_inode_num) {
            klog_write_pair("dir fail ", dir_name);
            fs_lock_leave();
            return 0;
        }
    }

    inode_num = find_file_in_dir(base_name, dir_inode_num);
    if (!inode_num) {
        klog_write_pair("file fail ", base_name);
        fs_lock_leave();
        return 0;
    }
    
    Ext2Inode inode;
    if (!read_inode(inode_num, &inode)) {
        klog_write("inode open fail");
        fs_lock_leave();
        return 0;
    }
    
    int fn_len = strlen(filename);
    if(fn_len > FS_FILENAME_LEN - 1) fn_len = FS_FILENAME_LEN - 1;

    for(int i=0; i<fn_len; i++) out_file->filename[i] = filename[i];
    out_file->filename[fn_len] = '\0';
    
    out_file->size = inode.i_size;
    out_file->inode_num = inode_num;
    out_file->type = (inode.i_mode & 0xF000) == 0x4000 ? 1 : 0;
    fs_lock_leave();
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

static int read_inode_range_locked(const Ext2Inode* inode, unsigned int start_pos, unsigned int size, void* buffer) {
    unsigned int bytes_read = 0;
    unsigned int file_size;
    unsigned int limit;
    unsigned int block_index = 0;

    if (!inode || !buffer) return 0;

    file_size = inode->i_size;
    if (file_size > (12 + 256) * 1024) {
        file_size = (12 + 256) * 1024;
    }
    if (start_pos >= file_size) return 0;

    limit = size;
    if (start_pos + limit > file_size) {
        limit = file_size - start_pos;
    }

    while (bytes_read < limit) {
        unsigned int block_num = 0;
        unsigned int block_offset;
        unsigned int to_copy;

        if (block_index < 12) {
            block_num = inode->i_block[block_index];
        } else {
            int indirect_index = (int)block_index - 12;
            if (inode->i_block[12] == 0 || indirect_index >= (1024 / 4)) break;
            if (indirect_index == 0) {
                read_block(inode->i_block[12], fs_indirect_entries_buf);
            }
            block_num = fs_indirect_entries_buf[indirect_index];
        }

        if (block_num == 0) break;

        read_block(block_num, fs_io_block_buf);
        block_offset = 0;
        if (start_pos > block_index * 1024) {
            block_offset = start_pos - block_index * 1024;
            if (block_offset >= 1024) {
                block_index++;
                continue;
            }
        }

        to_copy = limit - bytes_read;
        if (to_copy > 1024 - block_offset) {
            to_copy = 1024 - block_offset;
        }

        memcpy((unsigned char*)buffer + bytes_read, fs_io_block_buf + block_offset, (int)to_copy);
        bytes_read += to_copy;
        block_index++;
    }

    return (int)bytes_read;
}

// 支持 ext2 风格的 12 个直接块 + 1 个一级间接块
int fs_read_file(const char* filename, void* buffer) {
    SystemFile file;
    Ext2Inode inode;
    int ret;
    fs_lock_enter();
    if (!sys_file_open(filename, &file)) {
        fs_lock_leave();
        return 0;
    }

    if (!read_inode(file.inode_num, &inode)) {
        fs_lock_leave();
        return 0;
    }

    ret = read_inode_range_locked(&inode, 0, inode.i_size, buffer);
    fs_lock_leave();
    return ret;
}

int fs_write_file(const char* filename, const void* buffer, unsigned int size) {
    SystemFile file;
    Ext2Inode inode;
    unsigned int capacity;
    unsigned int* indirect_entries = 0;
    unsigned char* block_buf;
    const unsigned char* src = (const unsigned char*)buffer;
    unsigned int remaining = size;
    unsigned int total_blocks;

    fs_lock_enter();
    if (!filename) {
        fs_lock_leave();
        return 0;
    }
    if (size > 0 && !buffer) {
        fs_lock_leave();
        return 0;
    }
    if (!sys_file_open(filename, &file)) {
        fs_lock_leave();
        return 0;
    }
    if (file.type != 0) {
        fs_lock_leave();
        return 0;
    }
    if (!read_inode(file.inode_num, &inode)) {
        fs_lock_leave();
        return 0;
    }

    capacity = inode_data_capacity(&inode);
    if (size > capacity) {
        fs_lock_leave();
        return 0;
    }

    if (inode.i_block[12] != 0) {
        indirect_entries = (unsigned int*)malloc(1024);
        if (!indirect_entries) {
            fs_lock_leave();
            return 0;
        }
        read_block(inode.i_block[12], indirect_entries);
    }

    block_buf = (unsigned char*)malloc(1024);
    if (!block_buf) {
        if (indirect_entries) free(indirect_entries);
        fs_lock_leave();
        return 0;
    }

    total_blocks = capacity / 1024;
    for (unsigned int block_index = 0; block_index < total_blocks; block_index++) {
        unsigned int block_num;
        unsigned int copy_len = 0;

        if (block_index < 12) {
            block_num = inode.i_block[block_index];
        } else {
            if (!indirect_entries) break;
            block_num = indirect_entries[block_index - 12];
        }
        if (block_num == 0) break;

        memset(block_buf, 0, 1024);
        if (remaining > 0) {
            copy_len = remaining > 1024 ? 1024 : remaining;
            memcpy(block_buf, src, copy_len);
            src += copy_len;
            remaining -= copy_len;
        }
        write_block(block_num, block_buf);
    }

    free(block_buf);
    if (indirect_entries) free(indirect_entries);

    inode.i_size = size;
    if (!write_inode(file.inode_num, &inode)) {
        fs_lock_leave();
        return 0;
    }
    fs_lock_leave();
    return (int)size;
}

// App 读取接口 (支持流式读取)
int app_file_read(AppFile* file, void* buffer, unsigned int size) {
    Ext2Inode inode;
    int bytes_read;

    if (!fs_ready || !file) return 0;
    
    unsigned int bytes_to_read = size;
    // 防止读越界
    if (file->current_pos + size > file->sys_file.size) {
        bytes_to_read = file->sys_file.size - file->current_pos;
    }
    
    if (bytes_to_read == 0) return 0;

    fs_lock_enter();
    if (!read_inode(file->sys_file.inode_num, &inode)) {
        fs_lock_leave();
        return 0;
    }

    bytes_read = read_inode_range_locked(&inode, file->current_pos, bytes_to_read, buffer);
    fs_lock_leave();

    if (bytes_read > 0) {
        file->current_pos += (unsigned int)bytes_read;
    }

    return bytes_read;
}

int tsk_load(const char* filename, void** out_entry) {
    AppFile file;
    char actual_name[FS_FILENAME_LEN];
    int read_result;

    if (!filename) return 0;
    copy_trim_hidden_suffix(actual_name, filename, sizeof(actual_name));

    // 1. 打开文件
    if (!app_file_open(actual_name, &file)) {
        klog_write_pair("open miss ", actual_name);
        // 对隐藏文件提供回退：<name> 不存在时尝试 <name>._hid_
        if (!str_ends_with(filename, HIDDEN_SUFFIX)) {
            int n = strlen(actual_name);
            int sfx = strlen(HIDDEN_SUFFIX);
            if (n + sfx < FS_FILENAME_LEN) {
                for (int i = 0; i < sfx; i++) {
                    actual_name[n + i] = HIDDEN_SUFFIX[i];
                }
                actual_name[n + sfx] = '\0';
            }
        }

        if (!app_file_open(actual_name, &file)) {
            klog_write_pair("open fail ", actual_name);
            return 0;
        }
    }
    klog_write_pair("open ok ", actual_name);
    
    // 2. 获取文件大小
    unsigned int size = file.sys_file.size;
    if (size == 0) {
        klog_write_pair("size zero ", actual_name);
        return 0;
    }
    if (size > APP_SLOT_SIZE) {
        klog_write_pair("size too big ", actual_name);
        return 0;
    }
    
    // 3. flat binary 需要加载到与链接地址匹配的固定槽位
    void* load_addr = resolve_app_load_address(actual_name);
    if (!load_addr) {
        klog_write_pair("slot fail ", actual_name);
        return 0;
    }
    
    // 4. 【关键】直接读取整个文件到内存，不做任何 Header 解析
    // 因为 Makefile 里生成的已经是纯指令代码了
    read_result = app_file_read(&file, load_addr, size);
    if ((unsigned int)read_result != size) {
        klog_write_pair("read fail ", actual_name);
        return 0;
    }
    klog_write_pair("read ok ", actual_name);
    
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
        
        // 假设你有 kprintf
        // kprintf("File: %s (inode %d)\n", name, entry->inode);
        
        pos += entry->rec_len;
    }
    free(block_buf);
}


// 【修复】获取真实的文件列表
int fs_get_file_list(char* buffer, int max_len, const char* dir_path) {
    Ext2Inode dir_inode;
    unsigned int dir_inode_num;

    if (!buffer || max_len <= 0) return 0;
    buffer[0] = '\0';
    if (!fs_ready) {
        return 0;
    }

    dir_inode_num = resolve_dir_inode(dir_path);
    if (!dir_inode_num) return 0;
    if (!read_inode(dir_inode_num, &dir_inode)) return 0;
    if ((dir_inode.i_mode & 0xF000) != 0x4000) return 0;

    // 从堆分配
    char* block_buf = (char*)malloc(1024);
    if (!block_buf) {
        return 0;
    }
    read_block(dir_inode.i_block[0], block_buf);
    
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
    if (buf_idx >= max_len) buf_idx = max_len - 1;
    buffer[buf_idx] = '\0';
    free(block_buf); // 释放
    return 1;
}
