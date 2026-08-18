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
static unsigned int fs_write_allocated_blocks[12 + 256 + 1];

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

// 多级路径解析：从 EXT2_ROOT_INODE 出发，逐段解析以 '/' 分隔的路径。
// 解析成功时将最终 inode 号写入 *out_inode 并返回 1；失败返回 0。
// 支持: 前导/连续/尾随斜杠、"."、".."、以及根路径 "/"。
// 注意: "." 与 ".." 依赖于目录项中存储的 inode 号，因此目录必须包含
// 正确的 "." 和 ".." 项 (fs_mkdir 会创建它们；根目录的 ".." 指向自身)。
// 必须在 fs_lock 保护下调用 (内部会读盘)。
static int resolve_path(const char* path, unsigned int* out_inode) {
    unsigned int cur_inode;
    const char* p;

    if (!path || !out_inode) return 0;
    cur_inode = EXT2_ROOT_INODE;

    // 跳过前导斜杠
    p = path;
    while (*p == '/') p++;

    // 空路径或纯斜杠视为根目录
    if (*p == '\0') {
        *out_inode = EXT2_ROOT_INODE;
        return 1;
    }

    while (*p) {
        char comp[256];
        int clen = 0;
        unsigned int next_inode;

        // 提取一个路径分量
        while (*p && *p != '/' && clen < (int)sizeof(comp) - 1) {
            comp[clen++] = *p++;
        }
        comp[clen] = '\0';

        // 跳过连续斜杠
        while (*p == '/') p++;

        if (clen == 0) continue; // 不应发生，但保持防御

        // "." 保持当前目录
        if (strcmp(comp, ".") == 0) {
            // 若这是最后一个分量，则当前 inode 即为结果
            if (*p == '\0') {
                *out_inode = cur_inode;
                return 1;
            }
            continue;
        }

        // ".." 取当前目录的父目录项。注意 "."/".." 必须存在于目录块中。
        if (strcmp(comp, "..") == 0) {
            next_inode = find_file_in_dir("..", cur_inode);
            if (!next_inode) {
                // 未找到 ".."：若已是根，停留在根；否则失败
                if (cur_inode == EXT2_ROOT_INODE) {
                    if (*p == '\0') {
                        *out_inode = EXT2_ROOT_INODE;
                        return 1;
                    }
                    continue;
                }
                klog_write_pair("resolve .. fail ", path);
                return 0;
            }
            cur_inode = next_inode;
            if (*p == '\0') {
                *out_inode = cur_inode;
                return 1;
            }
            continue;
        }

        // 普通分量：在当前目录中查找
        next_inode = find_file_in_dir(comp, cur_inode);
        if (!next_inode) {
            klog_write_pair("resolve miss ", comp);
            return 0;
        }
        cur_inode = next_inode;

        if (*p == '\0') {
            *out_inode = cur_inode;
            return 1;
        }
    }

    // 理论上不会到达这里 (循环在 *p=='\0' 时即返回)
    *out_inode = cur_inode;
    return 1;
}

// 解析 path 的父目录，返回父目录 inode 号，并将最后一段分量 (文件/目录名)
// 写入 out_name。例如 "system/config.rtsk" -> 父目录 "system" 的 inode，
// out_name="config.rtsk"；"readme.txt" -> 父目录为根，out_name="readme.txt"。
// 失败返回 0。必须在 fs_lock 保护下调用。
static unsigned int resolve_parent_path(const char* path, char* out_name, int name_max) {
    const char* p;
    const char* last_start;
    const char* last_end;
    int name_len;

    if (!path || !out_name || name_max <= 0) return 0;
    out_name[0] = '\0';

    p = path;
    while (*p == '/') p++;
    if (*p == '\0') return 0; // 没有分量

    // 找到最后一个非空分量
    last_start = 0;
    while (*p) {
        const char* seg_start = p;
        while (*p && *p != '/') p++;
        const char* seg_end = p;
        while (*p == '/') p++;
        // 跳过纯 "." 与 ".." 作为叶子的情形: 它们不应作为要创建/删除的名字,
        // 但若路径以它们结尾，这里仍按最后一段处理 (后续查找会自然失败/成功)。
        last_start = seg_start;
        last_end = seg_end;
    }

    if (!last_start) return 0;

    name_len = (int)(last_end - last_start);
    if (name_len <= 0) return 0;
    if (name_len >= name_max) name_len = name_max - 1;
    memcpy(out_name, last_start, name_len);
    out_name[name_len] = '\0';

    // 父目录路径 = 去掉最后一段后的前缀
    {
        char parent_path[512];
        int parent_len = (int)(last_start - path);
        unsigned int parent_inode;
        // 跳过 path 开头到 last_start 之间的尾部斜杠
        while (parent_len > 0 && path[parent_len - 1] == '/') parent_len--;
        if (parent_len <= 0) {
            // 父目录就是根
            return EXT2_ROOT_INODE;
        }
        if (parent_len >= (int)sizeof(parent_path)) parent_len = (int)sizeof(parent_path) - 1;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';

        if (!resolve_path(parent_path, &parent_inode)) return 0;
        return parent_inode;
    }
}

static unsigned int resolve_dir_inode(const char* dir_path) {
    unsigned int inode;

    if (!dir_path || dir_path[0] == '\0') return EXT2_ROOT_INODE;

    if (!resolve_path(dir_path, &inode)) return 0;
    return inode;
}

static void* resolve_app_load_address(const char* filename) {
    char base_name[FS_FILENAME_LEN];
    const char* leaf;
    if (!filename) return 0;
    copy_trim_hidden_suffix(base_name, filename, sizeof(base_name));
    leaf = path_leaf(base_name);

    if (strcmp(leaf, "app.tsk") == 0) return (void*)MP_APP_LOAD_APP;
    if (strcmp(leaf, "terminal.tsk") == 0) return (void*)MP_APP_LOAD_TERMINAL;
    if (strcmp(leaf, "wm.tsk") == 0) return (void*)MP_APP_LOAD_WM;
    if (strcmp(leaf, "start.tsk") == 0) return (void*)MP_APP_LOAD_START;
    if (strcmp(leaf, "image.tsk") == 0) return (void*)MP_APP_LOAD_IMAGE;
    if (strcmp(leaf, "settings.tsk") == 0) return (void*)MP_APP_LOAD_SETTINGS;

    // 目前 flat binary 不是位置无关代码，未知 .tsk 无法安全并行加载
    return 0;
}

static int tsk_header_has_magic(const TskHeader* header) {
    if (!header) return 0;
    return strncmp(header->magic, TSK_MAGIC, 4) == 0;
}

static unsigned int tsk_checksum32(const unsigned char* data, unsigned int size) {
    unsigned int hash = 2166136261u;

    if (!data && size != 0) return 0;

    for (unsigned int i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static int tsk_load_addr_valid(unsigned int load_addr, unsigned int image_size) {
    unsigned int slot_end;

    if (image_size == 0 || image_size > MP_APP_SLOT_SIZE) return 0;
    if (load_addr < MP_APP_SLOT_BASE || load_addr >= MP_APP_SLOT_LIMIT) return 0;
    if ((load_addr - MP_APP_SLOT_BASE) % MP_APP_SLOT_SIZE != 0) return 0;

    slot_end = load_addr + MP_APP_SLOT_SIZE;
    if (slot_end < load_addr || slot_end > MP_APP_SLOT_LIMIT) return 0;

    return 1;
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


// ===== Allocation helpers (called within fs_lock) =====

static int write_superblock(void) {
    unsigned char* buf = (unsigned char*)malloc(1024);
    if (!buf) return 0;
    memset(buf, 0, 1024);
    memcpy(buf, &ext2_sb, sizeof(ext2_sb));
    disk_write_sectors(FS_BASE_SECTOR + 2, 2, buf);
    free(buf);
    return 1;
}

static int write_group_desc(void) {
    unsigned char* buf = (unsigned char*)malloc(1024);
    if (!buf) return 0;
    memset(buf, 0, 1024);
    memcpy(buf, &ext2_gd, sizeof(ext2_gd));
    disk_write_sectors(FS_BASE_SECTOR + 4, 2, buf);
    free(buf);
    return 1;
}

static unsigned int alloc_inode(void) {
    unsigned char* bitmap;
    unsigned int i;
    unsigned int total_bits;
    unsigned int start;

    if (!fs_ready) return 0;
    bitmap = (unsigned char*)malloc(1024);
    if (!bitmap) return 0;
    read_block(ext2_gd.bg_inode_bitmap, bitmap);

    total_bits = ext2_sb.s_inodes_per_group;
    if (total_bits > 1024 * 8) total_bits = 1024 * 8;
    start = ext2_sb.s_first_ino;
    if (start < 11) start = 11;

    for (i = start; i < total_bits; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));
            write_block(ext2_gd.bg_inode_bitmap, bitmap);
            free(bitmap);
            ext2_sb.s_free_inodes_count--;
            ext2_gd.bg_free_inodes_count--;
            write_superblock();
            write_group_desc();
            return i + 1;
        }
    }
    free(bitmap);
    return 0;
}

static void free_inode(unsigned int inode_num) {
    unsigned char* bitmap;
    unsigned int bit;

    if (!fs_ready || inode_num < 1) return;
    bitmap = (unsigned char*)malloc(1024);
    if (!bitmap) return;
    read_block(ext2_gd.bg_inode_bitmap, bitmap);

    bit = inode_num - 1;
    bitmap[bit / 8] &= ~(1 << (bit % 8));
    write_block(ext2_gd.bg_inode_bitmap, bitmap);
    free(bitmap);

    ext2_sb.s_free_inodes_count++;
    ext2_gd.bg_free_inodes_count++;
    write_superblock();
    write_group_desc();
}

static unsigned int alloc_block(void) {
    unsigned char* bitmap;
    unsigned int i;
    unsigned int total_bits;
    unsigned int inode_table_blocks;
    unsigned int first_data;

    if (!fs_ready) return 0;
    bitmap = (unsigned char*)malloc(1024);
    if (!bitmap) return 0;
    read_block(ext2_gd.bg_block_bitmap, bitmap);

    total_bits = ext2_sb.s_blocks_count;
    if (total_bits > 1024 * 8) total_bits = 1024 * 8;

    inode_table_blocks = (ext2_sb.s_inodes_per_group * ext2_sb.s_inode_size) / 1024;
    first_data = ext2_gd.bg_inode_table + inode_table_blocks;

    for (i = first_data; i < total_bits; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            unsigned char* zero_buf;
            bitmap[i / 8] |= (1 << (i % 8));
            write_block(ext2_gd.bg_block_bitmap, bitmap);
            free(bitmap);

            ext2_sb.s_free_blocks_count--;
            ext2_gd.bg_free_blocks_count--;
            write_superblock();
            write_group_desc();

            zero_buf = (unsigned char*)malloc(1024);
            if (zero_buf) {
                memset(zero_buf, 0, 1024);
                write_block(i, zero_buf);
                free(zero_buf);
            }
            return i;
        }
    }
    free(bitmap);
    return 0;
}

static void free_block(unsigned int block_num) {
    unsigned char* bitmap;

    if (!fs_ready || block_num == 0) return;
    bitmap = (unsigned char*)malloc(1024);
    if (!bitmap) return;
    read_block(ext2_gd.bg_block_bitmap, bitmap);

    bitmap[block_num / 8] &= ~(1 << (block_num % 8));
    write_block(ext2_gd.bg_block_bitmap, bitmap);
    free(bitmap);

    ext2_sb.s_free_blocks_count++;
    ext2_gd.bg_free_blocks_count++;
    write_superblock();
    write_group_desc();
}

static unsigned int dir_entry_actual_size(unsigned int name_len) {
    return (8 + name_len + 3) & ~3u;
}

static int dir_entry_bounds(const unsigned char* block, unsigned int pos,
                            unsigned int* out_rec_len, unsigned int* out_name_len) {
    unsigned int rec_len;
    unsigned int name_len;

    if (!block || !out_rec_len || !out_name_len) return 0;
    if ((pos & 3u) != 0 || pos > 1024u - 8u) return 0;

    rec_len = (unsigned int)block[pos + 4] |
              ((unsigned int)block[pos + 5] << 8);
    name_len = (unsigned int)block[pos + 6];

    if (rec_len < 8 || (rec_len & 3u) != 0) return 0;
    if (rec_len > 1024u - pos) return 0;
    if (name_len > 255u || name_len > rec_len - 8u) return 0;

    *out_rec_len = rec_len;
    *out_name_len = name_len;
    return 1;
}

static int add_dir_entry(unsigned int dir_inode_num, unsigned int target_inode,
                         const char* name, unsigned char file_type) {
    Ext2Inode dir_inode;
    unsigned char* block_buf;
    unsigned int pos;
    unsigned int name_len;
    unsigned int entry_len;

    if (!read_inode(dir_inode_num, &dir_inode)) return 0;
    if ((dir_inode.i_mode & 0xF000) != 0x4000 || dir_inode.i_block[0] == 0) return 0;

    name_len = strlen(name);
    if (name_len > 255) name_len = 255;
    entry_len = dir_entry_actual_size(name_len);

    block_buf = (unsigned char*)malloc(1024);
    if (!block_buf) return 0;
    read_block(dir_inode.i_block[0], block_buf);

    pos = 0;
    while (pos < 1024) {
        Ext2DirEntry* entry;
        unsigned int actual;
        unsigned int next_pos;
        unsigned int rec_len;
        unsigned int stored_name_len;

        if (!dir_entry_bounds(block_buf, pos, &rec_len, &stored_name_len)) {
            free(block_buf);
            return 0;
        }
        entry = (Ext2DirEntry*)(block_buf + pos);
        actual = dir_entry_actual_size(stored_name_len);
        next_pos = pos + rec_len;

        if (next_pos == 1024) {
            unsigned int new_pos = pos + actual;
            if (new_pos + entry_len <= 1024) {
                Ext2DirEntry* ne;
                entry->rec_len = (unsigned short)actual;
                ne = (Ext2DirEntry*)(block_buf + new_pos);
                ne->inode = target_inode;
                ne->rec_len = (unsigned short)(1024 - new_pos);
                ne->name_len = (unsigned char)name_len;
                ne->file_type = file_type;
                memcpy(ne->name, name, (int)name_len);
                write_block(dir_inode.i_block[0], block_buf);
                free(block_buf);
                return 1;
            }
            break;
        }
        pos = next_pos;
    }

    free(block_buf);
    return 0;
}

static int remove_dir_entry(unsigned int dir_inode_num, const char* name) {
    Ext2Inode dir_inode;
    unsigned char* block_buf;
    unsigned int pos = 0;
    unsigned int prev_pos = 0;

    if (!read_inode(dir_inode_num, &dir_inode)) return 0;
    if ((dir_inode.i_mode & 0xF000) != 0x4000 || dir_inode.i_block[0] == 0) return 0;

    block_buf = (unsigned char*)malloc(1024);
    if (!block_buf) return 0;
    read_block(dir_inode.i_block[0], block_buf);

    while (pos < 1024) {
        Ext2DirEntry* entry;
        char entry_name[256];
        unsigned int rec_len;
        unsigned int name_len;
        unsigned int i;

        if (!dir_entry_bounds(block_buf, pos, &rec_len, &name_len)) {
            free(block_buf);
            return 0;
        }
        entry = (Ext2DirEntry*)(block_buf + pos);
        for (i = 0; i < name_len; i++) entry_name[i] = entry->name[i];
        entry_name[i] = '\0';

        if (entry->inode != 0 && strcmp(entry_name, name) == 0) {
            if (prev_pos == pos) {
                entry->inode = 0;
            } else {
                Ext2DirEntry* prev_entry = (Ext2DirEntry*)(block_buf + prev_pos);
                prev_entry->rec_len = (unsigned short)(prev_entry->rec_len + rec_len);
            }
            write_block(dir_inode.i_block[0], block_buf);
            free(block_buf);
            return 1;
        }

        prev_pos = pos;
        pos += rec_len;
    }

    free(block_buf);
    return 0;
}

static unsigned int __attribute__((unused)) inode_data_capacity(const Ext2Inode* inode) {
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

// 返回目录 inode 的第 block_index 个数据块号 (支持直接块 0..11 与一级间接块)。
// block_index 从 0 开始；遍历完成后返回 0。
// indirect_buf 用于缓存一级间接块内容 (大小至少 1024 字节)。
static unsigned int dir_block_at(const Ext2Inode* dir_inode, unsigned int block_index,
                                 unsigned int* indirect_buf) {
    if (!dir_inode) return 0;

    if (block_index < 12) {
        return dir_inode->i_block[block_index];
    }

    if (dir_inode->i_block[12] == 0) return 0;
    block_index -= 12;
    if (block_index >= (1024 / 4)) return 0;
    if (!indirect_buf) return 0;

    read_block(dir_inode->i_block[12], indirect_buf);
    return indirect_buf[block_index];
}

// 内部：在目录中查找文件名 (支持多块目录: 直接块 0..11 + 一级间接块)
static int find_file_in_dir(const char* filename, unsigned int dir_inode_num) {
    Ext2Inode dir_inode;
    unsigned int block_index;
    unsigned int* indirect_buf = 0;
    int found = 0;

    if (!filename || !read_inode(dir_inode_num, &dir_inode)) {
        klog_write("read inode fail");
        return 0;
    }
    if ((dir_inode.i_mode & 0xF000) != 0x4000 || dir_inode.i_block[0] == 0) {
        klog_write("not a dir");
        return 0;
    }

    indirect_buf = (unsigned int*)malloc(1024);
    if (!indirect_buf) {
        // 内存不足时退化为只查第一个块
        read_block(dir_inode.i_block[0], fs_dir_block_buf);
        {
            unsigned int pos = 0;
            while (pos < 1024) {
                Ext2DirEntry* entry;
                unsigned int rec_len;
                unsigned int name_len;
                char name[256];

                if (!dir_entry_bounds(fs_dir_block_buf, pos, &rec_len, &name_len)) break;
                entry = (Ext2DirEntry*)(fs_dir_block_buf + pos);
                for (unsigned int i = 0; i < name_len; i++) name[i] = entry->name[i];
                name[name_len] = '\0';
                if (entry->inode != 0 && strcmp(name, filename) == 0) return entry->inode;
                pos += rec_len;
            }
        }
        klog_write_pair("dir miss ", filename);
        return 0;
    }

    for (block_index = 0; block_index < (12 + 256); block_index++) {
        unsigned int block_num;
        unsigned int pos;

        block_num = dir_block_at(&dir_inode, block_index, indirect_buf);
        if (block_num == 0) break;

        read_block(block_num, fs_dir_block_buf);
        pos = 0;
        while (pos < 1024) {
            Ext2DirEntry* entry;
            unsigned int rec_len;
            unsigned int name_len;
            char name[256];

            if (!dir_entry_bounds(fs_dir_block_buf, pos, &rec_len, &name_len)) break;
            entry = (Ext2DirEntry*)(fs_dir_block_buf + pos);
            for (unsigned int i = 0; i < name_len; i++) name[i] = entry->name[i];
            name[name_len] = '\0';

            if (entry->inode != 0 && strcmp(name, filename) == 0) {
                found = entry->inode;
                goto done;
            }
            pos += rec_len;
        }
    }

done:
    free(indirect_buf);
    if (!found) klog_write_pair("dir miss ", filename);
    return found;
}

// 打开系统文件 (获取 inode 信息)
int sys_file_open(const char* filename, SystemFile* out_file) {
    unsigned int inode_num;
    Ext2Inode inode;

    fs_lock_enter();
    if (!fs_ready) {
        fs_lock_leave();
        return 0;
    }

    if (!filename || !out_file) {
        klog_write_pair("path bad ", filename ? filename : "(null)");
        fs_lock_leave();
        return 0;
    }

    if (!resolve_path(filename, &inode_num)) {
        klog_write_pair("path bad ", filename);
        fs_lock_leave();
        return 0;
    }

    if (!read_inode(inode_num, &inode)) {
        klog_write("inode open fail");
        fs_lock_leave();
        return 0;
    }

    int fn_len = strlen(filename);
    if (fn_len > FS_FILENAME_LEN - 1) fn_len = FS_FILENAME_LEN - 1;

    for (int i = 0; i < fn_len; i++) out_file->filename[i] = filename[i];
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
    if (limit > file_size - start_pos) limit = file_size - start_pos;

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
int fs_read_file(const char* filename, void* buffer, unsigned int capacity) {
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

    ret = read_inode_range_locked(&inode, 0, capacity, buffer);
    fs_lock_leave();
    return ret;
}

int fs_write_file(const char* filename, const void* buffer, unsigned int size) {
    SystemFile file;
    Ext2Inode inode;
    unsigned int* indirect_entries = 0;
    unsigned int* allocated_blocks = fs_write_allocated_blocks;
    unsigned char* block_buf = fs_io_block_buf;
    const unsigned char* src = (const unsigned char*)buffer;
    unsigned int blocks_needed;
    unsigned int allocated_count = 0;
    unsigned int block_index;
    unsigned int max_file_size = (12u + 256u) * 1024u;
    int result = 0;

    fs_lock_enter();
    if (!filename || (size > 0 && !buffer) || size > max_file_size) goto out;
    if (!sys_file_open(filename, &file) || file.type != 0) goto out;
    if (!read_inode(file.inode_num, &inode)) goto out;

    blocks_needed = (size + 1023u) / 1024u;
    if (inode.i_block[12] != 0 || blocks_needed > 12) {
        indirect_entries = fs_indirect_entries_buf;

        if (inode.i_block[12] != 0) {
            read_block(inode.i_block[12], indirect_entries);
        } else {
            unsigned int indirect_block = alloc_block();
            if (!indirect_block) goto rollback;
            inode.i_block[12] = indirect_block;
            allocated_blocks[allocated_count++] = indirect_block;
            memset(indirect_entries, 0, 1024);
        }
    }

    // Reserve every missing block before overwriting existing file data.
    for (block_index = 0; block_index < blocks_needed; block_index++) {
        unsigned int* slot;
        unsigned int block_num;

        if (block_index < 12) {
            slot = &inode.i_block[block_index];
        } else {
            if (!indirect_entries) goto rollback;
            slot = &indirect_entries[block_index - 12];
        }

        if (*slot != 0) continue;
        block_num = alloc_block();
        if (!block_num) goto rollback;
        *slot = block_num;
        allocated_blocks[allocated_count++] = block_num;
    }

    if (blocks_needed > 0) {
        unsigned int remaining = size;

        for (block_index = 0; block_index < blocks_needed; block_index++) {
            unsigned int block_num;
            unsigned int copy_len = remaining > 1024u ? 1024u : remaining;

            block_num = block_index < 12
                ? inode.i_block[block_index]
                : indirect_entries[block_index - 12];
            if (!block_num) goto rollback;

            memset(block_buf, 0, 1024);
            if (copy_len > 0) {
                memcpy(block_buf, src, (int)copy_len);
                src += copy_len;
                remaining -= copy_len;
            }
            write_block(block_num, block_buf);
        }
    }

    // Truncation releases blocks that no longer belong to the file.
    for (block_index = blocks_needed; block_index < 12; block_index++) {
        if (inode.i_block[block_index] != 0) {
            free_block(inode.i_block[block_index]);
            inode.i_block[block_index] = 0;
        }
    }

    if (indirect_entries) {
        unsigned int first_unused = blocks_needed > 12 ? blocks_needed - 12 : 0;
        for (block_index = first_unused; block_index < 256; block_index++) {
            if (indirect_entries[block_index] != 0) {
                free_block(indirect_entries[block_index]);
                indirect_entries[block_index] = 0;
            }
        }

        if (blocks_needed > 12) {
            write_block(inode.i_block[12], indirect_entries);
        } else if (inode.i_block[12] != 0) {
            free_block(inode.i_block[12]);
            inode.i_block[12] = 0;
        }
    }

    inode.i_size = size;
    inode.i_blocks = blocks_needed * 2u + (inode.i_block[12] ? 2u : 0u);
    if (!write_inode(file.inode_num, &inode)) goto out;

    result = (int)size;
    goto out;

rollback:
    while (allocated_count > 0) {
        free_block(allocated_blocks[--allocated_count]);
    }

out:
    fs_lock_leave();
    return result;
}

// App 读取接口 (支持流式读取)
int app_file_read(AppFile* file, void* buffer, unsigned int size) {
    Ext2Inode inode;
    int bytes_read;

    if (!fs_ready || !file) return 0;

    unsigned int bytes_to_read;
    if (file->current_pos >= file->sys_file.size) return 0;

    bytes_to_read = size;
    if (bytes_to_read > file->sys_file.size - file->current_pos)
        bytes_to_read = file->sys_file.size - file->current_pos;

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

static int open_tsk_with_hidden_alias(const char* filename, AppFile* file,
                                      char actual_name[FS_FILENAME_LEN]) {
    if (!filename || !file || !actual_name) return 0;
    copy_trim_hidden_suffix(actual_name, filename, FS_FILENAME_LEN);
    if (!app_file_open(actual_name, file)) {
        klog_write_pair("open miss ", actual_name);
        if (!str_ends_with(filename, HIDDEN_SUFFIX)) {
            int n = strlen(actual_name);
            int sfx = strlen(HIDDEN_SUFFIX);
            if (n + sfx < FS_FILENAME_LEN) {
                for (int i = 0; i < sfx; i++) actual_name[n + i] = HIDDEN_SUFFIX[i];
                actual_name[n + sfx] = '\0';
            }
        }
        if (!app_file_open(actual_name, file)) {
            klog_write_pair("open fail ", actual_name);
            return 0;
        }
    }
    klog_write_pair("open ok ", actual_name);
    return 1;
}

int tsk_probe(const char* filename, TskImageInfo* out_info) {
    AppFile file;
    char actual_name[FS_FILENAME_LEN];
    TskHeader header;
    unsigned int raw_size;
    void* legacy_load;
    int header_bytes;

    if (!filename || !out_info) return 0;
    memset(out_info, 0, sizeof(*out_info));
    if (!open_tsk_with_hidden_alias(filename, &file, actual_name)) return 0;
    raw_size = file.sys_file.size;
    if (raw_size == 0) { klog_write_pair("size zero ", actual_name); return 0; }

    memset(&header, 0, sizeof(header));
    header_bytes = app_file_read(&file, &header, sizeof(header));
    if (header_bytes == (int)sizeof(header) && tsk_header_has_magic(&header)) {
        if (header.version != TSK_VERSION || header.flags != 0 ||
            !tsk_load_addr_valid(header.load_addr, header.image_size) ||
            header.entry_offset >= header.image_size ||
            raw_size < sizeof(TskHeader) ||
            header.image_size > raw_size - sizeof(TskHeader)) {
            klog_write_pair("hdr invalid ", actual_name);
            return 0;
        }
        out_info->load_addr = header.load_addr;
        out_info->entry_offset = header.entry_offset;
        out_info->image_size = header.image_size;
        out_info->inode_num = file.sys_file.inode_num;
        out_info->has_header = 1;
        return 1;
    }

    if (raw_size > MP_APP_SLOT_SIZE) { klog_write_pair("size too big ", actual_name); return 0; }
    legacy_load = resolve_app_load_address(actual_name);
    if (!legacy_load) { klog_write_pair("slot fail ", actual_name); return 0; }
    out_info->load_addr = (unsigned int)legacy_load;
    out_info->entry_offset = 0;
    out_info->image_size = raw_size;
    out_info->inode_num = file.sys_file.inode_num;
    out_info->has_header = 0;
    return 1;
}

int tsk_load_to(const char* filename, void* destination, const TskImageInfo* expected) {
    AppFile file;
    char actual_name[FS_FILENAME_LEN];
    TskHeader header;
    int read_result;

    if (!filename || !destination || !expected || expected->image_size == 0 ||
        expected->image_size > MP_APP_SLOT_SIZE) return 0;
    if (!open_tsk_with_hidden_alias(filename, &file, actual_name)) return 0;
    if (file.sys_file.inode_num != expected->inode_num) {
        klog_write_pair("inode changed ", actual_name);
        return 0;
    }

    memset(destination, 0, MP_APP_SLOT_SIZE);
    if (expected->has_header) {
        memset(&header, 0, sizeof(header));
        if (app_file_read(&file, &header, sizeof(header)) != (int)sizeof(header) ||
            !tsk_header_has_magic(&header) || header.version != TSK_VERSION ||
            header.flags != 0 || header.load_addr != expected->load_addr ||
            header.entry_offset != expected->entry_offset ||
            header.image_size != expected->image_size) {
            klog_write_pair("hdr changed ", actual_name);
            return 0;
        }
        read_result = app_file_read(&file, destination, expected->image_size);
        if ((unsigned int)read_result != expected->image_size ||
            tsk_checksum32((const unsigned char*)destination, expected->image_size) !=
                header.image_checksum) {
            memset(destination, 0, MP_APP_SLOT_SIZE);
            klog_write_pair("hdr data bad ", actual_name);
            return 0;
        }
        klog_write_pair("hdr read ok ", actual_name);
        return 1;
    }

    file.current_pos = 0;
    read_result = app_file_read(&file, destination, expected->image_size);
    if ((unsigned int)read_result != expected->image_size) {
        memset(destination, 0, MP_APP_SLOT_SIZE);
        klog_write_pair("read fail ", actual_name);
        return 0;
    }
    klog_write_pair("read ok ", actual_name);
    return 1;
}

int tsk_load(const char* filename, void** out_entry,
             unsigned int* out_load_base, unsigned int* out_image_size) {
    TskImageInfo info;
    if (!filename || !out_entry) return 0;
    if (out_load_base) *out_load_base = 0;
    if (out_image_size) *out_image_size = 0;
    if (!tsk_probe(filename, &info) ||
        !tsk_load_to(filename, (void*)info.load_addr, &info)) return 0;
    if (out_load_base) *out_load_base = info.load_addr;
    if (out_image_size) *out_image_size = info.image_size;
    *out_entry = (void*)(info.load_addr + info.entry_offset);
    return 1;
}


// ===== Public file management API =====

int fs_create_file(const char* path) {
    char base_name[FS_FILENAME_LEN];
    unsigned int dir_inode_num;
    unsigned int new_inode_num;
    Ext2Inode new_inode;

    fs_lock_enter();
    if (!fs_ready || !path) {
        fs_lock_leave();
        return 0;
    }

    dir_inode_num = resolve_parent_path(path, base_name, sizeof(base_name));
    if (!dir_inode_num || base_name[0] == '\0') { fs_lock_leave(); return 0; }

    if (find_file_in_dir(base_name, dir_inode_num)) {
        fs_lock_leave();
        return -1;
    }
    new_inode_num = alloc_inode();
    if (!new_inode_num) { fs_lock_leave(); return 0; }

    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.i_mode = 0x81A4;
    new_inode.i_size = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;
    if (!write_inode(new_inode_num, &new_inode)) {
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }
    if (!add_dir_entry(dir_inode_num, new_inode_num, base_name, EXT2_FT_REG_FILE)) {
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }
    fs_lock_leave();
    return 1;
}

int fs_delete_file(const char* path) {
    char base_name[FS_FILENAME_LEN];
    unsigned int dir_inode_num;
    unsigned int inode_num;
    Ext2Inode inode;
    unsigned int i;

    fs_lock_enter();
    if (!fs_ready || !path) { fs_lock_leave(); return 0; }

    dir_inode_num = resolve_parent_path(path, base_name, sizeof(base_name));
    if (!dir_inode_num || base_name[0] == '\0') { fs_lock_leave(); return 0; }

    inode_num = find_file_in_dir(base_name, dir_inode_num);
    if (!inode_num) { fs_lock_leave(); return 0; }
    if (!read_inode(inode_num, &inode)) { fs_lock_leave(); return 0; }
    if ((inode.i_mode & 0xF000) == 0x4000) { fs_lock_leave(); return -1; }

    for (i = 0; i < 12; i++) {
        if (inode.i_block[i] != 0) {
            free_block(inode.i_block[i]);
            inode.i_block[i] = 0;
        }
    }
    if (inode.i_block[12] != 0) {
        unsigned int* indirect = (unsigned int*)malloc(1024);
        if (indirect) {
            read_block(inode.i_block[12], indirect);
            for (i = 0; i < 256; i++) {
                if (indirect[i] != 0) free_block(indirect[i]);
            }
            free(indirect);
        }
        free_block(inode.i_block[12]);
    }

    memset(&inode, 0, sizeof(inode));
    inode.i_dtime = 1;
    write_inode(inode_num, &inode);
    free_inode(inode_num);
    remove_dir_entry(dir_inode_num, base_name);

    fs_lock_leave();
    return 1;
}

int fs_mkdir(const char* path) {
    char base_name[FS_FILENAME_LEN];
    unsigned int dir_inode_num;
    unsigned int new_inode_num;
    unsigned int new_block_num;
    Ext2Inode new_inode;
    unsigned char* block_buf;

    fs_lock_enter();
    if (!fs_ready || !path) { fs_lock_leave(); return 0; }

    dir_inode_num = resolve_parent_path(path, base_name, sizeof(base_name));
    if (!dir_inode_num || base_name[0] == '\0') { fs_lock_leave(); return 0; }

    if (find_file_in_dir(base_name, dir_inode_num)) { fs_lock_leave(); return -1; }

    new_inode_num = alloc_inode();
    if (!new_inode_num) { fs_lock_leave(); return 0; }
    new_block_num = alloc_block();
    if (!new_block_num) {
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }

    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.i_mode = 0x41ED;
    new_inode.i_size = 1024;
    new_inode.i_links_count = 2;
    new_inode.i_blocks = 2;
    new_inode.i_block[0] = new_block_num;
    if (!write_inode(new_inode_num, &new_inode)) {
        free_block(new_block_num);
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }

    block_buf = (unsigned char*)malloc(1024);
    if (!block_buf) {
        free_block(new_block_num);
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }
    memset(block_buf, 0, 1024);
    {
        Ext2DirEntry* dot = (Ext2DirEntry*)block_buf;
        dot->inode = new_inode_num;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';

        Ext2DirEntry* dotdot = (Ext2DirEntry*)(block_buf + 12);
        dotdot->inode = dir_inode_num;
        dotdot->rec_len = 1024 - 12;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
    }
    write_block(new_block_num, block_buf);
    free(block_buf);

    if (!add_dir_entry(dir_inode_num, new_inode_num, base_name, EXT2_FT_DIR)) {
        free_block(new_block_num);
        free_inode(new_inode_num);
        fs_lock_leave();
        return 0;
    }
    {
        Ext2Inode parent_inode;
        if (read_inode(dir_inode_num, &parent_inode)) {
            parent_inode.i_links_count++;
            write_inode(dir_inode_num, &parent_inode);
        }
    }
    ext2_gd.bg_used_dirs_count++;
    write_group_desc();

    fs_lock_leave();
    return 1;
}


int fs_rename(const char* old_path, const char* new_path) {
    char old_base_name[FS_FILENAME_LEN];
    char new_base_name[FS_FILENAME_LEN];
    unsigned int old_dir_inode_num;
    unsigned int new_dir_inode_num;
    unsigned int file_inode_num;
    unsigned int old_name_len;
    unsigned int new_name_len;
    unsigned int old_actual;
    unsigned int new_actual;
    int can_inplace = 0;
    Ext2Inode file_inode;

    fs_lock_enter();
    if (!fs_ready || !old_path || !new_path) { fs_lock_leave(); return 0; }

    old_dir_inode_num = resolve_parent_path(old_path, old_base_name, sizeof(old_base_name));
    if (!old_dir_inode_num || old_base_name[0] == '\0') { fs_lock_leave(); return 0; }
    new_dir_inode_num = resolve_parent_path(new_path, new_base_name, sizeof(new_base_name));
    if (!new_dir_inode_num || new_base_name[0] == '\0') { fs_lock_leave(); return 0; }

    file_inode_num = find_file_in_dir(old_base_name, old_dir_inode_num);
    if (!file_inode_num) { fs_lock_leave(); return 0; }

    if (find_file_in_dir(new_base_name, new_dir_inode_num)) {
        fs_lock_leave();
        return -1;
    }

    old_name_len = strlen(old_base_name);
    new_name_len = strlen(new_base_name);
    old_actual = dir_entry_actual_size(old_name_len);
    new_actual = dir_entry_actual_size(new_name_len);

    if (old_dir_inode_num == new_dir_inode_num) {
        /* Try in-place update only if new name fits within the old entry's
         * actual storage (so the following entry's offset is unaffected). */
        if (new_actual <= old_actual) {
            Ext2Inode dir_inode;
            unsigned char* block_buf;
            unsigned int pos = 0;

            if (!read_inode(old_dir_inode_num, &dir_inode)) { fs_lock_leave(); return 0; }
            if (dir_inode.i_block[0] == 0) { fs_lock_leave(); return 0; }

            block_buf = (unsigned char*)malloc(1024);
            if (!block_buf) { fs_lock_leave(); return 0; }
            read_block(dir_inode.i_block[0], block_buf);

            can_inplace = 0;
            while (pos < 1024) {
                Ext2DirEntry* entry;
                unsigned int rec_len;
                unsigned int stored_name_len;
                char entry_name[256];
                unsigned int i;

                if (!dir_entry_bounds(block_buf, pos, &rec_len, &stored_name_len)) break;
                entry = (Ext2DirEntry*)(block_buf + pos);
                for (i = 0; i < stored_name_len; i++) entry_name[i] = entry->name[i];
                entry_name[i] = '\0';

                if (entry->inode != 0 && strcmp(entry_name, old_base_name) == 0) {
                    unsigned int pad_len;
                    unsigned int j;

                    entry->name_len = (unsigned char)new_name_len;
                    for (j = 0; j < new_name_len; j++) entry->name[j] = new_base_name[j];
                    pad_len = old_actual - 8 - new_name_len;
                    for (j = 0; j < pad_len; j++) entry->name[new_name_len + j] = 0;
                    write_block(dir_inode.i_block[0], block_buf);
                    can_inplace = 1;
                    break;
                }
                pos += rec_len;
            }
            free(block_buf);

            if (can_inplace) {
                if (read_inode(file_inode_num, &file_inode)) {
                    file_inode.i_mtime = 1; /* simplified timestamp */
                    write_inode(file_inode_num, &file_inode);
                }
                fs_lock_leave();
                return 1;
            }
            /* fall back to remove+add below */
        }

        if (!remove_dir_entry(old_dir_inode_num, old_base_name)) {
            fs_lock_leave();
            return 0;
        }
        if (!add_dir_entry(new_dir_inode_num, file_inode_num, new_base_name, EXT2_FT_REG_FILE)) {
            /* best-effort restore */
            add_dir_entry(old_dir_inode_num, file_inode_num, old_base_name, EXT2_FT_REG_FILE);
            fs_lock_leave();
            return 0;
        }
        if (read_inode(file_inode_num, &file_inode)) {
            file_inode.i_mtime = 1;
            write_inode(file_inode_num, &file_inode);
        }
        fs_lock_leave();
        return 1;
    }

    /* Different directories: remove from old, add to new. */
    if (!remove_dir_entry(old_dir_inode_num, old_base_name)) {
        fs_lock_leave();
        return 0;
    }
    if (!add_dir_entry(new_dir_inode_num, file_inode_num, new_base_name, EXT2_FT_REG_FILE)) {
        /* best-effort restore */
        add_dir_entry(old_dir_inode_num, file_inode_num, old_base_name, EXT2_FT_REG_FILE);
        fs_lock_leave();
        return 0;
    }
    if (read_inode(file_inode_num, &file_inode)) {
        file_inode.i_mtime = 1;
        write_inode(file_inode_num, &file_inode);
    }
    fs_lock_leave();
    return 1;
}


int fs_move(const char* old_path, const char* new_dir_path) {
    char old_base_name[FS_FILENAME_LEN];
    unsigned int old_dir_inode_num;
    unsigned int new_dir_inode_num;
    unsigned int file_inode_num;
    Ext2Inode new_dir_inode;
    Ext2Inode file_inode;

    fs_lock_enter();
    if (!fs_ready || !old_path || !new_dir_path) { fs_lock_leave(); return 0; }

    old_dir_inode_num = resolve_parent_path(old_path, old_base_name, sizeof(old_base_name));
    if (!old_dir_inode_num || old_base_name[0] == '\0') { fs_lock_leave(); return 0; }

    file_inode_num = find_file_in_dir(old_base_name, old_dir_inode_num);
    if (!file_inode_num) { fs_lock_leave(); return 0; }

    new_dir_inode_num = resolve_dir_inode(new_dir_path);
    if (!new_dir_inode_num) { fs_lock_leave(); return 0; }
    if (!read_inode(new_dir_inode_num, &new_dir_inode)) { fs_lock_leave(); return 0; }
    if ((new_dir_inode.i_mode & 0xF000) != 0x4000 || new_dir_inode.i_block[0] == 0) {
        fs_lock_leave(); return 0;
    }

    /* If new name already exists in target directory, fail. */
    if (find_file_in_dir(old_base_name, new_dir_inode_num)) { fs_lock_leave(); return -1; }

    if (!remove_dir_entry(old_dir_inode_num, old_base_name)) {
        fs_lock_leave();
        return 0;
    }
    if (!add_dir_entry(new_dir_inode_num, file_inode_num, old_base_name, EXT2_FT_REG_FILE)) {
        /* best-effort restore */
        add_dir_entry(old_dir_inode_num, file_inode_num, old_base_name, EXT2_FT_REG_FILE);
        fs_lock_leave();
        return 0;
    }
    if (read_inode(file_inode_num, &file_inode)) {
        file_inode.i_mtime = 1;
        write_inode(file_inode_num, &file_inode);
    }
    fs_lock_leave();
    return 1;
}



// 修复后的 fs_list_files
void fs_list_files() {
    Ext2Inode root_inode;
    char* block_buf;
    unsigned int pos = 0;

    if (!fs_ready || !read_inode(EXT2_ROOT_INODE, &root_inode)) return;
    if (root_inode.i_block[0] == 0) return;

    block_buf = (char*)malloc(1024);
    if (!block_buf) return;
    read_block(root_inode.i_block[0], block_buf);

    while (pos < 1024) {
        unsigned int rec_len;
        unsigned int name_len;
        if (!dir_entry_bounds((const unsigned char*)block_buf, pos, &rec_len, &name_len)) break;
        (void)name_len;
        pos += rec_len;
    }
    free(block_buf);
}


// 【修复】获取真实的文件列表 (支持跨多个数据块的目录)
int fs_get_file_list(char* buffer, int max_len, const char* dir_path) {
    Ext2Inode dir_inode;
    unsigned int dir_inode_num;
    char* block_buf;
    unsigned int* indirect_buf = 0;
    unsigned int block_index;
    int buf_idx = 0;

    if (!buffer || max_len <= 0) return 0;
    buffer[0] = '\0';
    if (!fs_ready) return 0;

    fs_lock_enter();
    dir_inode_num = resolve_dir_inode(dir_path);
    if (!dir_inode_num || !read_inode(dir_inode_num, &dir_inode)) {
        fs_lock_leave();
        return 0;
    }
    if ((dir_inode.i_mode & 0xF000) != 0x4000 || dir_inode.i_block[0] == 0) {
        fs_lock_leave();
        return 0;
    }

    block_buf = (char*)malloc(1024);
    if (!block_buf) {
        fs_lock_leave();
        return 0;
    }
    indirect_buf = (unsigned int*)malloc(1024);
    if (!indirect_buf) {
        // 退化为只读第一个块
        unsigned int pos = 0;
        read_block(dir_inode.i_block[0], block_buf);
        while (pos < 1024) {
            Ext2DirEntry* entry;
            unsigned int rec_len;
            unsigned int name_len;
            char name[256];
            int l;

            if (!dir_entry_bounds((const unsigned char*)block_buf, pos, &rec_len, &name_len)) break;
            entry = (Ext2DirEntry*)(block_buf + pos);
            l = (int)name_len;
            for (int i = 0; i < l; i++) name[i] = entry->name[i];
            name[l] = '\0';

            if (entry->inode != 0 && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                int needed = l + 1;
                if (buf_idx + needed < max_len) {
                    for (int i = 0; i < l; i++) buffer[buf_idx++] = name[i];
                    buffer[buf_idx++] = '\n';
                }
            }
            pos += rec_len;
        }
        if (buf_idx >= max_len) buf_idx = max_len - 1;
        buffer[buf_idx] = '\0';
        free(block_buf);
        fs_lock_leave();
        return 1;
    }

    // 遍历所有直接块 (0..11) 与一级间接块
    for (block_index = 0; block_index < (12 + 256); block_index++) {
        unsigned int block_num;
        unsigned int pos;

        block_num = dir_block_at(&dir_inode, block_index, indirect_buf);
        if (block_num == 0) break;

        read_block(block_num, block_buf);
        pos = 0;
        while (pos < 1024) {
            Ext2DirEntry* entry;
            unsigned int rec_len;
            unsigned int name_len;
            char name[256];
            int l;

            if (!dir_entry_bounds((const unsigned char*)block_buf, pos, &rec_len, &name_len)) break;
            entry = (Ext2DirEntry*)(block_buf + pos);
            l = (int)name_len;
            for (int i = 0; i < l; i++) name[i] = entry->name[i];
            name[l] = '\0';

            if (entry->inode != 0 && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                int needed = l + 1;
                if (buf_idx + needed < max_len) {
                    for (int i = 0; i < l; i++) buffer[buf_idx++] = name[i];
                    buffer[buf_idx++] = '\n';
                }
            }
            pos += rec_len;
        }
    }

    if (buf_idx >= max_len) buf_idx = max_len - 1;
    buffer[buf_idx] = '\0';
    free(block_buf);
    free(indirect_buf);
    fs_lock_leave();
    return 1;
}
