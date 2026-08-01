#ifndef TLX_H
#define TLX_H

/*
 * Tsuki Linux eXtension (TLX)
 *
 * This is an intentionally independent compatibility surface.  It borrows
 * familiar operating-system semantics without claiming Linux ABI identity.
 */
#define TLX_ABI_VERSION 1u

#define TLX_MAX_PATH 64
#define TLX_MAX_FDS 8

#define TLX_STD_INPUT  0
#define TLX_STD_OUTPUT 1
#define TLX_STD_ERROR  2

#define TLX_OPEN_READ   0x0001u
#define TLX_OPEN_WRITE  0x0002u
#define TLX_OPEN_CREATE 0x0004u
#define TLX_OPEN_TRUNC  0x0008u
#define TLX_OPEN_APPEND 0x0010u
#define TLX_OPEN_DIR    0x0020u

#define TLX_SEEK_START 0
#define TLX_SEEK_HERE  1
#define TLX_SEEK_END   2

#define TLX_EPERM       1
#define TLX_ENOENT      2
#define TLX_EIO         5
#define TLX_EBAD_HANDLE 9
#define TLX_EBUSY       16
#define TLX_EEXIST      17
#define TLX_ENOT_DIR    20
#define TLX_EIS_DIR     21
#define TLX_EINVAL      22
#define TLX_EMFILE      24
#define TLX_ENOSPC      28
#define TLX_ENOSYS      38
#define TLX_ENOT_EMPTY  39
#define TLX_EOVERFLOW   75

#define TLX_OP_OPEN     1
#define TLX_OP_CLOSE    2
#define TLX_OP_READ     3
#define TLX_OP_WRITE    4
#define TLX_OP_SEEK     5
#define TLX_OP_FSTAT    6
#define TLX_OP_LIST     7
#define TLX_OP_GETPID   8
#define TLX_OP_GETPPID  9
#define TLX_OP_SLEEP    10
#define TLX_OP_YIELD    11
#define TLX_OP_CLOCK    12
#define TLX_OP_GETCWD   13
#define TLX_OP_CHDIR    14
#define TLX_OP_SPAWN    15
#define TLX_OP_IDENTITY 16

#define TLX_HANDLE_KIND_INPUT  1
#define TLX_HANDLE_KIND_OUTPUT 2
#define TLX_HANDLE_KIND_ERROR  3
#define TLX_HANDLE_KIND_FILE   4
#define TLX_HANDLE_KIND_DIR    5

typedef struct {
    unsigned char used;
    unsigned char kind;
    unsigned short reserved;
    unsigned int flags;
    unsigned int position;
    unsigned int size;
    unsigned int inode;
    char path[TLX_MAX_PATH];
} TlxHandle;

typedef struct {
    unsigned int size;
    unsigned int position;
    unsigned int kind;
    unsigned int flags;
} TlxFileInfo;

typedef struct {
    char name[24];
    char release[24];
    unsigned int abi_version;
    unsigned int feature_bits;
} TlxIdentity;

int tlx_open(const char* path, unsigned int flags);
int tlx_close(int handle);
int tlx_read(int handle, void* buffer, unsigned int size);
int tlx_write(int handle, const void* buffer, unsigned int size);
int tlx_seek(int handle, int offset, int whence);
int tlx_fstat(int handle, TlxFileInfo* info);
int tlx_list(const char* path, char* buffer, unsigned int capacity);
int tlx_getpid(void);
int tlx_getppid(void);
int tlx_sleep(unsigned int ticks);
int tlx_yield(void);
unsigned int tlx_clock(void);
int tlx_getcwd(char* buffer, unsigned int capacity);
int tlx_chdir(const char* path);
int tlx_spawn(const char* path);
int tlx_identity(TlxIdentity* identity);

#endif
