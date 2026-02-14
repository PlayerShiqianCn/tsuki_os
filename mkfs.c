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

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: ./mkfs <output.img> <boot.bin> <kernel.bin> <files...>\n");
        return 1;
    }

    // --- 收集文件信息 ---
    typedef struct {
        char name[256];
        int size;
    } FileInfo;
    FileInfo files[64];
    int file_count = 0;
    
    for (int i = 4; i < argc && file_count < 64; i++) {
        FILE* f = fopen(argv[i], "rb");
        if (!f) { printf("Warning: Cannot open %s\n", argv[i]); continue; }
        
        fseek(f, 0, SEEK_END);
        int size = ftell(f);
        fclose(f);
        
        // 提取文件名
        char* path = argv[i];
        char* name = strrchr(path, '/');
        if (name) name++; else name = path;
        
        strcpy(files[file_count].name, name);
        files[file_count].size = size;
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
    for(int i=0; i<file_count; i++) total_data_blocks += (files[i].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int total_blocks = 100 + total_data_blocks; // 预留一些
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
    sb.s_free_inodes_count = sb.s_inodes_count - 10 - file_count;
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
    gd.bg_used_dirs_count = 1; // Root

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
    int reserved_blocks = 5 + (inodes_per_group * 128 / BLOCK_SIZE) + total_data_blocks + 10;
    for(int i=0; i<reserved_blocks; i++) {
         block_bitmap[i/8] |= (1 << (i%8));
    }
    fwrite(block_bitmap, 1, BLOCK_SIZE, out);

    // 3.5 写入 Inode Bitmap (Block 4)
    char inode_bitmap[BLOCK_SIZE];
    memset(inode_bitmap, 0, BLOCK_SIZE);
    // 1-10 resv, 11+ user files. 
    // Inode 2 is Root.
    inode_bitmap[0] = 0xFF; // 0-7 used
    inode_bitmap[1] = 0x03; // 8-9 (reserved), 10 (reserved) - simplified
    // 实际上我们需要标记 inode 2 和 inode 11...11+count
    memset(inode_bitmap, 0, BLOCK_SIZE);
    inode_bitmap[0] = 0x07; // 1, 2, Used. (bit 0 is inode 1)
    // 标记文件 inodes
    for(int i=0; i<file_count; i++) {
        int idx = 10 + i; // inode 11 is index 10
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
    root->i_links_count = 2;
    int inode_table_blocks = inode_table_len / BLOCK_SIZE;
    int root_data_block = 5 + inode_table_blocks; // 紧接着 inode table
    root->i_blocks = 2; // 512b sectors
    root->i_block[0] = root_data_block;

    // --- File Inodes (Inode 11+, index 10+) ---
    int current_data_block = root_data_block + 1;

    for (int i=0; i<file_count; i++) {
        Ext2Inode* file_node = (Ext2Inode*)(inode_table + (10 + i) * 128);
        file_node->i_mode = 0x81C0; // File
        file_node->i_size = files[i].size;
        file_node->i_links_count = 1;
        int blocks_needed = (files[i].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        file_node->i_blocks = blocks_needed * 2; // 512b sectors count
        
        for(int b=0; b<blocks_needed && b<12; b++) {
            file_node->i_block[b] = current_data_block++;
        }
    }

    fwrite(inode_table, 1, inode_table_len, out);
    free(inode_table);

    // 3.7 写入 Root Directory Data Block
    char root_dir[BLOCK_SIZE];
    memset(root_dir, 0, BLOCK_SIZE);
    int pos = 0;

    // Entry: .
    Ext2DirEntry* de = (Ext2DirEntry*)root_dir;
    de->inode = 2; de->name_len = 1; de->file_type = 2; strcpy(de->name, ".");
    de->rec_len = 12; pos += de->rec_len;

    // Entry: ..
    de = (Ext2DirEntry*)(root_dir + pos);
    de->inode = 2; de->name_len = 2; de->file_type = 2; strcpy(de->name, "..");
    de->rec_len = 12; pos += de->rec_len;

    // Files
    for (int i=0; i<file_count; i++) {
        de = (Ext2DirEntry*)(root_dir + pos);
        de->inode = 11 + i;
        de->name_len = strlen(files[i].name);
        de->file_type = 1;
        strcpy(de->name, files[i].name);
        
        int entry_len = 8 + de->name_len;
        if (entry_len % 4) entry_len += 4 - (entry_len % 4);
        de->rec_len = entry_len;
        
        if (i == file_count - 1) {
            // Last entry takes rest of block
            de->rec_len = BLOCK_SIZE - pos;
        }
        
        pos += de->rec_len;
    }
    fwrite(root_dir, 1, BLOCK_SIZE, out);

    // 3.8 写入文件数据
    for (int i=0; i<file_count; i++) {
        FILE* f = fopen(argv[i + 4], "rb");
        char fbuf[BLOCK_SIZE];
        int remain = files[i].size;
        while(remain > 0) {
            memset(fbuf, 0, BLOCK_SIZE);
            int n = (remain > BLOCK_SIZE) ? BLOCK_SIZE : remain;
            fread(fbuf, 1, n, f);
            fwrite(fbuf, 1, BLOCK_SIZE, out); // Always write full block padding
            remain -= BLOCK_SIZE;
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
