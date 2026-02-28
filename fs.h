#ifndef FS_H
#define FS_H

#include "disk.h"
#define APP_SLOT_SIZE         0x20000
#define APP_LOAD_APP          0x300000
#define APP_LOAD_TERMINAL     0x320000
#define APP_LOAD_WM           0x340000
#define APP_LOAD_START        0x360000
#define APP_LOAD_IMAGE        0x380000
#define APP_LOAD_SETTINGS     0x3A0000
// ----------------------------------------------------
// 【关键】定义文件系统分区起始扇区 (1MB = 2048 扇区)
// ----------------------------------------------------
#define FS_BASE_SECTOR 2048

// ==================== ext2 数据结构 ====================

// ext2 超级块 (128字节，偏移 1024)
typedef struct {
    unsigned int s_inodes_count;      // inode 总数
    unsigned int s_blocks_count;      // 块总数
    unsigned int s_r_blocks_count;    // 保留块数
    unsigned int s_free_blocks_count; // 空闲块数
    unsigned int s_free_inodes_count; // 空闲 inode 数
    unsigned int s_first_data_block;  // 第一个数据块号 (通常是 0 或 1)
    unsigned int s_log_block_size;    // 块大小 (0=1KB, 1=2KB, 2=4KB)
    unsigned int s_log_frag_size;     // 片大小
    unsigned int s_blocks_per_group;  // 每组块数
    unsigned int s_frags_per_group;   // 每组片数
    unsigned int s_inodes_per_group;  // 每组 inode 数
    unsigned int s_mtime;             // 最后挂载时间
    unsigned int s_wtime;             // 最后写入时间
    
    unsigned short s_mnt_count;       // 挂载次数
    unsigned short s_max_mnt_count;   // 最大挂载次数
    unsigned short s_magic;           // 魔数 0xEF53
    unsigned short s_state;           // 文件系统状态
    unsigned short s_errors;          // 错误处理方式
    unsigned short s_minor_rev_level; // 次版本号
    
    unsigned int s_lastcheck;         // 最后检查时间
    unsigned int s_checkinterval;     // 检查间隔
    unsigned int s_creator_os;        // 创建系统
    unsigned int s_rev_level;         // 版本号
    
    unsigned short s_def_resuid;      // 默认保留用户ID
    unsigned short s_def_resgid;      // 默认保留组ID
    
    // 以下是扩展字段 (rev 1+)
    unsigned int s_first_ino;         // 第一个非保留 inode
    unsigned short s_inode_size;      // inode 大小
    unsigned short s_block_group_nr;  // 块组编号
    unsigned int s_feature_compat;    // 兼容特性
    unsigned int s_feature_incompat;  // 不兼容特性
    unsigned int s_feature_ro_compat; // 只读兼容特性
    unsigned char s_uuid[16];         // 文件系统 UUID
    char s_volume_name[16];           // 卷名
    char s_last_mounted[64];          // 最后挂载路径
    unsigned int s_algorithm_usage_bitmap; // 算法使用位图
    unsigned char s_prealloc_blocks;  // 预分配块数
    unsigned char s_prealloc_dir_blocks; // 目录预分配块数
    unsigned short s_reserved_gdt_blocks; // 保留的块组描述符块数
    unsigned char s_journal_uuid[16]; // 日志 UUID
    unsigned int s_journal_inum;      // 日志 inode
    unsigned int s_journal_dev;       // 日志设备
    unsigned int s_last_orphan;       // 最后 orphan inode
    unsigned int s_reserved[190];     // 填充到 1024 字节
} Ext2SuperBlock;

// ext2 inode
typedef struct {
    unsigned short i_mode;        // 文件模式
    unsigned short i_uid;         // 用户 ID
    unsigned int i_size;          // 文件大小 (字节)
    unsigned int i_atime;         // 访问时间
    unsigned int i_ctime;         // 创建时间
    unsigned int i_mtime;         // 修改时间
    unsigned int i_dtime;         // 删除时间
    unsigned short i_gid;         // 组 ID
    unsigned short i_links_count; // 硬链接数
    unsigned int i_blocks;        // 使用的块数
    unsigned int i_flags;         // 标志
    unsigned int i_osd1;          // OS 相关
    unsigned int i_block[15];     // 块指针 (直接 + 一级 + 二级)
    unsigned int i_generation;    // 代号
    unsigned int i_file_acl;      // 文件 ACL 块
    unsigned int i_dir_acl;       // 目录 ACL 块
    unsigned int i_faddr;         // 碎片地址
    unsigned char i_osd2[12];     // OS 相关
} Ext2Inode;

// ext2 目录项
typedef struct {
    unsigned int inode;           // inode 号
    unsigned short rec_len;       // 目录项长度
    unsigned char name_len;       // 文件名长度
    unsigned char file_type;      // 文件类型
    char name[255];               // 文件名 (可变长度)
} Ext2DirEntry;

// ext2 块组描述符
typedef struct {
    unsigned int bg_block_bitmap;     // 块位图块号
    unsigned int bg_inode_bitmap;     // inode 位图块号
    unsigned int bg_inode_table;      // inode 表起始块号
    unsigned short bg_free_blocks_count; // 空闲块数
    unsigned short bg_free_inodes_count; // 空闲 inode 数
    unsigned short bg_used_dirs_count;   // 目录数
    unsigned short bg_pad;                // 填充
    unsigned int bg_reserved[3];          // 保留
} Ext2GroupDesc;

// ==================== 常量 ====================

#define EXT2_SUPERBLOCK_OFFSET 1024
#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INODE 2

// 文件类型
#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHRDEV 3
#define EXT2_FT_BLKDEV 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_SYMLINK 7

// ==================== 我们的简化文件系统 ====================

#define FS_MAX_FILES 64
#define FS_FILENAME_LEN 64

// TSK 文件头 (用于 .tsk 后缀的可执行文件)
typedef struct {
    char magic[4];      // "TSK"
    unsigned int entry; // 程序入口地址
    unsigned int size;  // 程序大小
    unsigned int reserved[1];
} TskHeader;

#define TSK_MAGIC "TSK"

// 简化的文件条目 (用于缓存)
typedef struct {
    char filename[FS_FILENAME_LEN];
    unsigned int size;
    unsigned int start_block;
    unsigned char type;  // 0=文件, 1=目录
} FileEntry;

// 封装 app_file 和 system_file
typedef struct {
    char filename[FS_FILENAME_LEN];
    unsigned int size;
    unsigned int inode_num;
    unsigned char type;
} SystemFile;

typedef struct {
    SystemFile sys_file;
    unsigned int current_pos;
} AppFile;

// 简化的超级块 (放在内存中)
typedef struct {
    unsigned int file_count;
    FileEntry files[FS_MAX_FILES];
} SimpleSuperBlock;

// ==================== 函数声明 ====================

void fs_init();
int fs_is_ready(void);
void fs_list_files();
int fs_get_file_list(char* buffer, int max_len, const char* dir_path);
int fs_read_file(const char* filename, void* buffer);
int fs_write_file(const char* filename, const void* buffer, unsigned int size);

// 封装接口
int sys_file_open(const char* filename, SystemFile* out_file);
int app_file_open(const char* filename, AppFile* out_file);
int app_file_read(AppFile* file, void* buffer, unsigned int size);

// TSK 加载
int tsk_load(const char* filename, void** out_entry);

#endif
