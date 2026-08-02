#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char filename[64];
    unsigned int size;
    unsigned int inode_num;
    unsigned char type;
} SystemFile;

void fs_init(void);
int fs_is_ready(void);
int fs_read_file(const char* filename, void* buffer, unsigned int capacity);
int fs_write_file(const char* filename, const void* buffer, unsigned int size);
int sys_file_open(const char* filename, SystemFile* out_file);

int test_disk_open(const char* path);
void test_disk_close(void);
unsigned int test_free_blocks(void);
void test_fill_block_bitmap(void);
void test_corrupt_root_rec_len(unsigned short rec_len);

static int open_fs(const char* image) {
    if (!test_disk_open(image)) return 0;
    fs_init();
    return fs_is_ready();
}

static int test_bounded_read(const char* image) {
    struct {
        unsigned char data[4];
        unsigned int canary;
    } output;
    unsigned char payload[64];
    int result;

    if (!open_fs(image)) return 1;
    memset(payload, 'B', sizeof(payload));
    if (fs_write_file("system/config.rtsk", payload, sizeof(payload)) != (int)sizeof(payload)) return 1;
    memset(&output, 0, sizeof(output));
    output.canary = 0x51a7c0deu;
    result = fs_read_file("system/config.rtsk", output.data, sizeof(output.data));
    test_disk_close();
    if (result != 4 || output.canary != 0x51a7c0deu || memcmp(output.data, "BBBB", 4) != 0) {
        fprintf(stderr, "FAIL bounded_read result=%d canary=%08x\n", result, output.canary);
        return 1;
    }
    puts("PASS bounded_read");
    return 0;
}

static int test_shrink(const char* image) {
    unsigned char payload[2050];
    unsigned char out[8];
    SystemFile file;
    unsigned int free_before;
    unsigned int free_after_grow;
    unsigned int free_after_shrink;
    int result;

    if (!open_fs(image)) return 1;
    memset(payload, 'L', sizeof(payload));
    free_before = test_free_blocks();
    if (fs_write_file("system/config.rtsk", payload, sizeof(payload)) != (int)sizeof(payload)) return 1;
    free_after_grow = test_free_blocks();
    result = fs_write_file("system/config.rtsk", "abc", 3);
    free_after_shrink = test_free_blocks();
    if (!sys_file_open("system/config.rtsk", &file)) return 1;
    memset(out, 0, sizeof(out));
    if (fs_read_file("system/config.rtsk", out, sizeof(out)) != 3) return 1;
    test_disk_close();
    if (result != 3 || file.size != 3 || memcmp(out, "abc", 3) != 0 ||
        free_after_grow + 2 != free_before || free_after_shrink != free_before) {
        fprintf(stderr, "FAIL shrink result=%d size=%u free=%u/%u/%u\n",
                result, file.size, free_before, free_after_grow, free_after_shrink);
        return 1;
    }
    puts("PASS shrink_and_reclaim");
    return 0;
}

static int test_truncate_zero(const char* image) {
    SystemFile file;
    unsigned int before;
    unsigned int after;

    if (!open_fs(image)) return 1;
    before = test_free_blocks();
    if (fs_write_file("system/config.rtsk", NULL, 0) != 0) return 1;
    after = test_free_blocks();
    if (!sys_file_open("system/config.rtsk", &file)) return 1;
    test_disk_close();
    if (file.size != 0 || after != before + 1) {
        fprintf(stderr, "FAIL truncate_zero size=%u free=%u/%u\n", file.size, before, after);
        return 1;
    }
    puts("PASS truncate_zero");
    return 0;
}

static int test_allocation_failure(const char* image) {
    unsigned char payload[2048];
    unsigned char out[8] = {0};
    SystemFile file;
    int result;

    if (!open_fs(image)) return 1;
    if (fs_write_file("system/config.rtsk", "old", 3) != 3) return 1;
    test_fill_block_bitmap();
    memset(payload, 'N', sizeof(payload));
    result = fs_write_file("system/config.rtsk", payload, sizeof(payload));
    if (!sys_file_open("system/config.rtsk", &file)) return 1;
    if (fs_read_file("system/config.rtsk", out, sizeof(out)) != 3) return 1;
    test_disk_close();
    if (result != 0 || file.size != 3 || memcmp(out, "old", 3) != 0) {
        fprintf(stderr, "FAIL allocation_failure result=%d size=%u data=%c%c%c\n",
                result, file.size, out[0], out[1], out[2]);
        return 1;
    }
    puts("PASS allocation_failure_atomic");
    return 0;
}

static int test_bad_directory(const char* image) {
    SystemFile file;
    int result;

    if (!open_fs(image)) return 1;
    test_corrupt_root_rec_len(1022);
    result = sys_file_open("system/version.txt", &file);
    test_disk_close();
    if (result != 0) {
        fprintf(stderr, "FAIL bad_directory accepted\n");
        return 1;
    }
    puts("PASS bad_directory rejected");
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s TEST IMAGE\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "bounded") == 0) return test_bounded_read(argv[2]);
    if (strcmp(argv[1], "shrink") == 0) return test_shrink(argv[2]);
    if (strcmp(argv[1], "truncate") == 0) return test_truncate_zero(argv[2]);
    if (strcmp(argv[1], "alloc-fail") == 0) return test_allocation_failure(argv[2]);
    if (strcmp(argv[1], "bad-dir") == 0) return test_bad_directory(argv[2]);
    return 2;
}
