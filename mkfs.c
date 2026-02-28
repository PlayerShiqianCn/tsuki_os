// mkfs.c - 修复版
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define BLOCK_SIZE 1024
#define EXT2_MAGIC 0xEF53

// 定义文件系统在磁盘上的起始偏移 (1MB = 2048 扇区)
// 这给内核留了 1MB 的空间，足够大了
#define FS_START_OFFSET (1024 * 1024) 

// -----------------------------------------------------------
// 请复制你原有的结构体定义 (Ext2SuperBlock, Ext2Inode, Ext2DirEntry, Ext2GroupDesc) 到这里
// 为节省篇幅，这里省略结构体定义，请确保代码里包含它们
// -----------------------------------------------------------
// (此处插入你原来 mkfs.c 里的结构体定义)

// 文件类型定义
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_reserved[190];
} Ext2SuperBlock;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} Ext2Inode;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} Ext2DirEntry;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} Ext2GroupDesc;

typedef struct {
    char dir[64];
    char name[256];
    int size;
    int blocks_needed;
    int indirect_blocks;
    int first_data_block;
    int indirect_block_num;
} FileInfo;

typedef struct {
    char name[64];
    int inode_num;
    int data_block;
    int file_count;
} DirInfo;

static const char* image_path_from_arg(const char* arg) {
    if (!arg) return "";
    if (strncmp(arg, ".fsroot/", 8) == 0) return arg + 8;
    {
        const char* name = strrchr(arg, '/');
        return name ? (name + 1) : arg;
    }
}

static void split_image_path(const char* image_path, char* dir, int dir_max, char* name, int name_max) {
    const char* slash;
    int dir_len;
    int name_len;

    if (!dir || !name || dir_max <= 0 || name_max <= 0) return;
    dir[0] = '\0';
    name[0] = '\0';
    if (!image_path || !image_path[0]) return;

    slash = strrchr(image_path, '/');
    if (!slash) {
        strncpy(name, image_path, name_max - 1);
        name[name_max - 1] = '\0';
        return;
    }

    dir_len = (int)(slash - image_path);
    if (dir_len >= dir_max) dir_len = dir_max - 1;
    memcpy(dir, image_path, dir_len);
    dir[dir_len] = '\0';

    name_len = strlen(slash + 1);
    if (name_len >= name_max) name_len = name_max - 1;
    memcpy(name, slash + 1, name_len);
    name[name_len] = '\0';
}

static void write_dir_entry(char* block, int* pos, uint32_t inode, uint8_t file_type, const char* name, int is_last) {
    Ext2DirEntry* de;
    int entry_len;

    if (!block || !pos || !name) return;

    de = (Ext2DirEntry*)(block + *pos);
    de->inode = inode;
    de->name_len = (uint8_t)strlen(name);
    de->file_type = file_type;
    strcpy(de->name, name);

    entry_len = 8 + de->name_len;
    if (entry_len % 4) entry_len += 4 - (entry_len % 4);
    de->rec_len = is_last ? (BLOCK_SIZE - *pos) : entry_len;
    *pos += de->rec_len;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: ./mkfs <output.img> <boot.bin> <kernel.bin> <files...>\n");
        return 1;
    }

    FileInfo files[64];
    DirInfo dirs[16];
    int file_count = 0;
    int dir_count = 0;
    
    for (int i = 4; i < argc && file_count < 64; i++) {
        const char* image_path;
        FILE* f = fopen(argv[i], "rb");
        if (!f) { printf("Warning: Cannot open %s\n", argv[i]); continue; }
        
        fseek(f, 0, SEEK_END);
        int size = ftell(f);
        fclose(f);
        
        image_path = image_path_from_arg(argv[i]);
        split_image_path(image_path, files[file_count].dir, sizeof(files[file_count].dir),
                         files[file_count].name, sizeof(files[file_count].name));
        if (files[file_count].name[0] == '\0') {
            printf("Warning: Invalid image path %s\n", argv[i]);
            continue;
        }

        if (files[file_count].dir[0]) {
            int found = -1;
            if (strchr(files[file_count].dir, '/')) {
                printf("Error: Nested directories are not supported: %s\n", image_path);
                return 1;
            }
            for (int d = 0; d < dir_count; d++) {
                if (strcmp(dirs[d].name, files[file_count].dir) == 0) {
                    found = d;
                    break;
                }
            }
            if (found < 0) {
                if (dir_count >= (int)(sizeof(dirs) / sizeof(dirs[0]))) {
                    printf("Error: Too many top-level directories.\n");
                    return 1;
                }
                strcpy(dirs[dir_count].name, files[file_count].dir);
                dirs[dir_count].inode_num = 0;
                dirs[dir_count].data_block = 0;
                dirs[dir_count].file_count = 0;
                found = dir_count;
                dir_count++;
            }
            dirs[found].file_count++;
        }

        files[file_count].size = size;
        files[file_count].blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (files[file_count].blocks_needed > (12 + 256)) {
            printf("Error: File too large for single indirect blocks: %s\n", image_path);
            return 1;
        }
        files[file_count].indirect_blocks = (files[file_count].blocks_needed > 12) ? 1 : 0;
        files[file_count].first_data_block = 0;
        files[file_count].indirect_block_num = 0;
        file_count++;
    }

    // --- 打开输出文件 ---
    FILE* out = fopen(argv[1], "wb");
    if (!out) { perror("fopen output"); return 1; }

    // 1. 写入引导扇区 (0 ~ 512)
    FILE* boot = fopen(argv[2], "rb");
    if (!boot) { perror("boot.bin"); return 1; }
    char buf[512];
    memset(buf, 0, 512);
    fread(buf, 1, 512, boot);
    fwrite(buf, 1, 512, out);
    fclose(boot);

    // 2. 写入内核 (512 ~ ...)
    FILE* kern = fopen(argv[3], "rb");
    if (!kern) { perror("kernel.bin"); return 1; }
    fseek(kern, 0, SEEK_END);
    int kern_size = ftell(kern);
    fseek(kern, 0, SEEK_SET);
    
    char* kern_buf = malloc(kern_size);
    fread(kern_buf, 1, kern_size, kern);
    fwrite(kern_buf, 1, kern_size, out);
    free(kern_buf);
    fclose(kern);

    // 3. 【关键修复】填充 0 直到文件系统起始位置 (FS_START_OFFSET)
    // 这样内核可以随便变大，只要不超过 1MB
    long current_pos = ftell(out);
    if (current_pos > FS_START_OFFSET) {
        printf("Error: Kernel is too big for the reserved space!\n");
        return 1;
    }
    
    // 使用 seek 和 write 0 来填充
    char zero = 0;
    fseek(out, FS_START_OFFSET - 1, SEEK_SET);
    fwrite(&zero, 1, 1, out); // 扩展文件大小
    
    // 现在文件指针在 FS_START_OFFSET，相当于这里是分区的开始 (Offset 0 of Partition)

    // --- 开始构建 Ext2 (相对于 FS_START_OFFSET) ---

    int inodes_per_group = 128;
    int blocks_per_group = 8192; // 8MB per group
    
    // 计算需要的块数
    int total_data_blocks = 0;
    for(int i=0; i<file_count; i++) {
        total_data_blocks += files[i].blocks_needed + files[i].indirect_blocks;
    }
    int total_blocks = 100 + total_data_blocks + dir_count; // 预留一些
    if (total_blocks < 64) total_blocks = 64; // 最小限制

    int num_groups = 1; // 简单起见，只有1个块组

    // 3.1 写入 Block 0 (Boot block of the FS, 1024 bytes empty)
    // 实际上 Ext2 的 Superblock 位于 1024 字节偏移处。
    // 如果 Block Size = 1024，Block 0 是引导块，Block 1 是 Superblock。
    char empty_block[BLOCK_SIZE];
    memset(empty_block, 0, BLOCK_SIZE);
    fwrite(empty_block, 1, BLOCK_SIZE, out); // Write Block 0

    // 3.2 写入 Superblock (Block 1)
    Ext2SuperBlock sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = num_groups * inodes_per_group;
    sb.s_blocks_count = blocks_per_group;
    sb.s_free_blocks_count = sb.s_blocks_count - total_blocks; // 粗略计算
    sb.s_free_inodes_count = sb.s_inodes_count - 10 - file_count - dir_count;
    sb.s_first_data_block = 1; // 1KB Block size -> SB is on block 1
    sb.s_log_block_size = 0;   // 1024 bytes
    sb.s_blocks_per_group = blocks_per_group;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_magic = EXT2_MAGIC;
    sb.s_rev_level = 0;
    sb.s_state = 1;
    sb.s_first_ino = 11;
    sb.s_inode_size = 128;

    fwrite(&sb, 1, sizeof(sb), out);
    // 填充 Superblock 到 1024 字节
    char sb_pad[BLOCK_SIZE - sizeof(sb)];
    memset(sb_pad, 0, sizeof(sb_pad));
    fwrite(sb_pad, 1, sizeof(sb_pad), out);

    // 3.3 写入 Group Descriptors (Block 2)
    Ext2GroupDesc gd;
    memset(&gd, 0, sizeof(gd));
    gd.bg_block_bitmap = 3;
    gd.bg_inode_bitmap = 4;
    gd.bg_inode_table = 5;
    gd.bg_free_blocks_count = sb.s_free_blocks_count;
    gd.bg_free_inodes_count = sb.s_free_inodes_count;
    gd.bg_used_dirs_count = 1 + dir_count; // Root + top-level dirs

    fwrite(&gd, 1, sizeof(gd), out);
    // 填充 GD 到 Block 结束
    char gd_pad[BLOCK_SIZE - sizeof(gd)];
    memset(gd_pad, 0, sizeof(gd_pad));
    fwrite(gd_pad, 1, sizeof(gd_pad), out);

    // 3.4 写入 Block Bitmap (Block 3)
    // 简单起见，我们假设前 100 个块都被占用了 (Metadata + Files)
    char block_bitmap[BLOCK_SIZE];
    memset(block_bitmap, 0, BLOCK_SIZE);
    // 标记前 N 个位
    int reserved_blocks = 5 + (inodes_per_group * 128 / BLOCK_SIZE) + total_data_blocks + 10 + dir_count;
    for(int i=0; i<reserved_blocks; i++) {
         block_bitmap[i/8] |= (1 << (i%8));
    }
    fwrite(block_bitmap, 1, BLOCK_SIZE, out);

    // 3.5 写入 Inode Bitmap (Block 4)
    char inode_bitmap[BLOCK_SIZE];
    memset(inode_bitmap, 0, BLOCK_SIZE);
    // 1-10 resv, 11+ user files. 
    // Inode 2 is Root.
    memset(inode_bitmap, 0, BLOCK_SIZE);
    inode_bitmap[0] = 0x07; // 1, 2, Used. (bit 0 is inode 1)
    for (int d = 0; d < dir_count; d++) {
        int idx = 10 + d;
        inode_bitmap[idx / 8] |= (1 << (idx % 8));
    }
    // 标记文件 inodes
    for(int i=0; i<file_count; i++) {
        int idx = 10 + dir_count + i;
        inode_bitmap[idx/8] |= (1 << (idx%8));
    }
    fwrite(inode_bitmap, 1, BLOCK_SIZE, out);

    // 3.6 写入 Inode Table (Block 5 ... )
    int inode_table_len = inodes_per_group * 128; // bytes
    char* inode_table = malloc(inode_table_len);
    memset(inode_table, 0, inode_table_len);

    // --- Root Inode (Inode 2, index 1) ---
    Ext2Inode* root = (Ext2Inode*)(inode_table + 128); // Index 1
    root->i_mode = 0x41ED; // Dir
    root->i_size = BLOCK_SIZE; // 1 block for dir entries
    root->i_links_count = 2 + dir_count;
    int inode_table_blocks = inode_table_len / BLOCK_SIZE;
    int root_data_block = 5 + inode_table_blocks; // 紧接着 inode table
    root->i_blocks = 2; // 512b sectors
    root->i_block[0] = root_data_block;

    int file_inode_base = 11 + dir_count;
    int file_data_start_block = root_data_block + 1 + dir_count;

    for (int d = 0; d < dir_count; d++) {
        Ext2Inode* subdir = (Ext2Inode*)(inode_table + (10 + d) * 128);
        dirs[d].inode_num = 11 + d;
        dirs[d].data_block = root_data_block + 1 + d;
        subdir->i_mode = 0x41ED; // Dir
        subdir->i_size = BLOCK_SIZE;
        subdir->i_links_count = 2;
        subdir->i_blocks = 2;
        subdir->i_block[0] = dirs[d].data_block;
    }

    // --- File Inodes (Inode 11+, index 10+) ---
    int current_data_block = file_data_start_block;

    for (int i=0; i<file_count; i++) {
        Ext2Inode* file_node = (Ext2Inode*)(inode_table + (file_inode_base - 1 + i) * 128);
        file_node->i_mode = 0x81C0; // File
        file_node->i_size = files[i].size;
        file_node->i_links_count = 1;
        int blocks_needed = files[i].blocks_needed;
        file_node->i_blocks = (blocks_needed + files[i].indirect_blocks) * 2; // 512b sectors count
        files[i].first_data_block = current_data_block;
        files[i].indirect_block_num = 0;

        for(int b=0; b<blocks_needed && b<12; b++) {
            file_node->i_block[b] = current_data_block + b;
        }

        if (blocks_needed > 12) {
            int indirect_block = current_data_block + 12;
            files[i].indirect_block_num = indirect_block;
            file_node->i_block[12] = indirect_block;
        }

        current_data_block += blocks_needed + files[i].indirect_blocks;
    }

    fwrite(inode_table, 1, inode_table_len, out);
    free(inode_table);

    // 3.7 写入 Root Directory Data Block
    char root_dir[BLOCK_SIZE];
    memset(root_dir, 0, BLOCK_SIZE);
    int pos = 0;
    int root_visible_files = 0;
    int root_entries_total = 0;

    for (int i = 0; i < file_count; i++) {
        if (files[i].dir[0] == '\0') root_visible_files++;
    }
    root_entries_total = root_visible_files + dir_count;

    write_dir_entry(root_dir, &pos, 2, EXT2_FT_DIR, ".", 0);
    write_dir_entry(root_dir, &pos, 2, EXT2_FT_DIR, "..", root_entries_total == 0);

    {
        int written = 0;
        for (int i = 0; i < file_count; i++) {
            int is_last;
            if (files[i].dir[0] != '\0') continue;
            written++;
            is_last = (written == root_entries_total);
            write_dir_entry(root_dir, &pos, file_inode_base + i, EXT2_FT_REG_FILE, files[i].name, is_last);
        }
    }

    for (int d = 0; d < dir_count; d++) {
        int entry_index = root_visible_files + d + 1;
        write_dir_entry(root_dir, &pos, dirs[d].inode_num, EXT2_FT_DIR, dirs[d].name,
                        entry_index == root_entries_total);
    }

    fwrite(root_dir, 1, BLOCK_SIZE, out);

    for (int d = 0; d < dir_count; d++) {
        char subdir_block[BLOCK_SIZE];
        int subpos = 0;

        memset(subdir_block, 0, BLOCK_SIZE);
        write_dir_entry(subdir_block, &subpos, dirs[d].inode_num, EXT2_FT_DIR, ".", 0);
        write_dir_entry(subdir_block, &subpos, 2, EXT2_FT_DIR, "..", dirs[d].file_count == 0);

        {
            int written = 0;
            for (int i = 0; i < file_count; i++) {
                if (strcmp(files[i].dir, dirs[d].name) != 0) continue;
                written++;
                write_dir_entry(subdir_block, &subpos, file_inode_base + i, EXT2_FT_REG_FILE,
                                files[i].name, written == dirs[d].file_count);
            }
        }

        fwrite(subdir_block, 1, BLOCK_SIZE, out);
    }

    // 3.8 写入文件数据
    for (int i=0; i<file_count; i++) {
        FILE* f = fopen(argv[i + 4], "rb");
        char fbuf[BLOCK_SIZE];
        int remain = files[i].size;
        int block_index = 0;

        while(remain > 0 && block_index < files[i].blocks_needed && block_index < 12) {
            memset(fbuf, 0, BLOCK_SIZE);
            int n = (remain > BLOCK_SIZE) ? BLOCK_SIZE : remain;
            fread(fbuf, 1, n, f);
            fwrite(fbuf, 1, BLOCK_SIZE, out); // Always write full block padding
            remain -= n;
            block_index++;
        }

        if (files[i].indirect_blocks) {
            unsigned int indirect_entries[BLOCK_SIZE / 4];
            memset(indirect_entries, 0, sizeof(indirect_entries));
            for (int j = 12; j < files[i].blocks_needed; j++) {
                indirect_entries[j - 12] = files[i].indirect_block_num + (j - 12) + 1;
            }
            fwrite(indirect_entries, 1, BLOCK_SIZE, out);
        }

        while(remain > 0 && block_index < files[i].blocks_needed) {
            memset(fbuf, 0, BLOCK_SIZE);
            {
                int n = (remain > BLOCK_SIZE) ? BLOCK_SIZE : remain;
                fread(fbuf, 1, n, f);
                fwrite(fbuf, 1, BLOCK_SIZE, out);
                remain -= n;
            }
            block_index++;
        }
        fclose(f);
    }

    // Pad total image size to 10MB
    fseek(out, 10*1024*1024 - 1, SEEK_SET);
    fputc(0, out);
    
    fclose(out);
    printf("Image created. FS starts at 1MB.\n");
    return 0;
}
