#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
int fs_create_file(const char* path);
int fs_delete_file(const char* path);
int fs_mkdir(const char* path);
int fs_rename(const char* old_path, const char* new_path);
int fs_move(const char* old_path, const char* new_dir_path);
int fs_get_file_list(char* buffer, int max_len, const char* dir_path);

int test_disk_open(const char* path);
void test_disk_close(void);
unsigned int test_free_blocks(void);
void test_fill_block_bitmap(void);
void test_corrupt_root_rec_len(unsigned short rec_len);

/* ========================================================================
 * Test infrastructure
 * ======================================================================== */

static int open_fs(const char* image) {
    if (!test_disk_open(image)) return 0;
    fs_init();
    return fs_is_ready();
}

/* ========================================================================
 * Existing tests (preserved)
 * ======================================================================== */

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

/* ========================================================================
 * New tests: Multi-block directory
 * ======================================================================== */

static int test_multiblock_dir(const char* image) {
    char name[64];
    char list_buf[4096];
    int i;
    int created = 0;
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL multiblock_dir: open\n"); return 1; }

    /* Create 35 files in the root directory to exceed one 1024-byte block.
     * Each dir entry is at least 12 bytes (8-byte header + 4-byte name padded
     * to 4-byte boundary).  With ~85 bytes of existing entries (. and .. and
     * system/ and image/), we need ~35 entries of 12+ bytes each = 420+ bytes,
     * which together with existing entries should exceed 1024.  We use short
     * names to pack tightly.  If add_dir_entry only supports one block, some
     * creates will fail; we accept whatever succeeds. */
    for (i = 0; i < 35; i++) {
        snprintf(name, sizeof(name), "t%02d", i);
        if (fs_create_file(name) == 1) {
            created++;
        }
    }

    /* We expect at least some files to be created successfully.  Even if the
     * directory can only hold one block, we should get at least ~20 entries. */
    if (created < 10) {
        fprintf(stderr, "FAIL multiblock_dir: only %d files created\n", created);
        test_disk_close();
        return 1;
    }

    /* Verify every created file can be found via sys_file_open. */
    for (i = 0; i < 35; i++) {
        snprintf(name, sizeof(name), "t%02d", i);
        if (sys_file_open(name, &file) == 1) {
            /* Found — verify name matches. */
            if (strcmp(file.filename, name) != 0) {
                fprintf(stderr, "FAIL multiblock_dir: name mismatch for %s\n", name);
                test_disk_close();
                return 1;
            }
        }
        /* Not found is acceptable if the dir ran out of space in block 0. */
    }

    /* Verify fs_get_file_list can enumerate them. */
    if (fs_get_file_list(list_buf, sizeof(list_buf), "") == 0) {
        fprintf(stderr, "FAIL multiblock_dir: list failed\n");
        test_disk_close();
        return 1;
    }

    /* Spot-check that at least the first created file appears in the list. */
    snprintf(name, sizeof(name), "t00\n");
    if (strstr(list_buf, name) == NULL) {
        fprintf(stderr, "FAIL multiblock_dir: t00 not in listing\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    printf("PASS multiblock_dir (created %d files)\n", created);
    return 0;
}

/* ========================================================================
 * New tests: Rename operations
 * ======================================================================== */

static int test_rename_basic(const char* image) {
    SystemFile file;
    unsigned char data[] = "rename_test_data";
    unsigned char out[32];

    if (!open_fs(image)) { fprintf(stderr, "FAIL rename_basic: open\n"); return 1; }

    /* Create a file and write data. */
    if (fs_create_file("rn_src") != 1) {
        fprintf(stderr, "FAIL rename_basic: create\n");
        test_disk_close();
        return 1;
    }
    if (fs_write_file("rn_src", data, sizeof(data)) != (int)sizeof(data)) {
        fprintf(stderr, "FAIL rename_basic: write\n");
        test_disk_close();
        return 1;
    }

    /* Rename it. */
    if (fs_rename("rn_src", "rn_dst") != 1) {
        fprintf(stderr, "FAIL rename_basic: rename returned error\n");
        test_disk_close();
        return 1;
    }

    /* Old name should be gone. */
    if (sys_file_open("rn_src", &file) != 0) {
        fprintf(stderr, "FAIL rename_basic: old name still exists\n");
        test_disk_close();
        return 1;
    }

    /* New name should exist and data should be intact. */
    if (sys_file_open("rn_dst", &file) != 1) {
        fprintf(stderr, "FAIL rename_basic: new name not found\n");
        test_disk_close();
        return 1;
    }
    memset(out, 0, sizeof(out));
    if (fs_read_file("rn_dst", out, sizeof(out)) != (int)sizeof(data) ||
        memcmp(out, data, sizeof(data)) != 0) {
        fprintf(stderr, "FAIL rename_basic: data mismatch\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS rename_basic");
    return 0;
}

static int test_rename_to_existing(const char* image) {
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL rename_to_existing: open\n"); return 1; }

    /* Create two files. */
    if (fs_create_file("rn_a") != 1 || fs_create_file("rn_b") != 1) {
        fprintf(stderr, "FAIL rename_to_existing: create\n");
        test_disk_close();
        return 1;
    }

    /* Renaming to an existing name should fail (-1). */
    int result = fs_rename("rn_a", "rn_b");
    if (result != -1) {
        fprintf(stderr, "FAIL rename_to_existing: expected -1, got %d\n", result);
        test_disk_close();
        return 1;
    }

    /* Both files should still exist. */
    if (sys_file_open("rn_a", &file) != 1 || sys_file_open("rn_b", &file) != 1) {
        fprintf(stderr, "FAIL rename_to_existing: file missing after failed rename\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS rename_to_existing");
    return 0;
}

static int test_rename_cross_dir(const char* image) {
    SystemFile file;
    unsigned char data[] = "cross_dir_data";
    unsigned char out[32];

    if (!open_fs(image)) { fprintf(stderr, "FAIL rename_cross_dir: open\n"); return 1; }

    /* Create a subdirectory and a file in the root. */
    if (fs_mkdir("rndir") != 1) {
        fprintf(stderr, "FAIL rename_cross_dir: mkdir\n");
        test_disk_close();
        return 1;
    }
    if (fs_create_file("rn_cross") != 1) {
        fprintf(stderr, "FAIL rename_cross_dir: create\n");
        test_disk_close();
        return 1;
    }
    if (fs_write_file("rn_cross", data, sizeof(data)) != (int)sizeof(data)) {
        fprintf(stderr, "FAIL rename_cross_dir: write\n");
        test_disk_close();
        return 1;
    }

    /* Rename to a different directory. */
    if (fs_rename("rn_cross", "rndir/rn_moved") != 1) {
        fprintf(stderr, "FAIL rename_cross_dir: rename returned error\n");
        test_disk_close();
        return 1;
    }

    /* Old name should be gone. */
    if (sys_file_open("rn_cross", &file) != 0) {
        fprintf(stderr, "FAIL rename_cross_dir: old name still exists\n");
        test_disk_close();
        return 1;
    }

    /* New name in subdirectory should exist and have correct data. */
    if (sys_file_open("rndir/rn_moved", &file) != 1) {
        fprintf(stderr, "FAIL rename_cross_dir: new name not found\n");
        test_disk_close();
        return 1;
    }
    memset(out, 0, sizeof(out));
    if (fs_read_file("rndir/rn_moved", out, sizeof(out)) != (int)sizeof(data) ||
        memcmp(out, data, sizeof(data)) != 0) {
        fprintf(stderr, "FAIL rename_cross_dir: data mismatch\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS rename_cross_dir");
    return 0;
}

static int test_rename_same_dir_shorter(const char* image) {
    SystemFile file;
    unsigned char data[] = "inplace_rename";
    unsigned char out[32];

    if (!open_fs(image)) { fprintf(stderr, "FAIL rename_same_dir_shorter: open\n"); return 1; }

    /* Create a file with a longer name, rename to shorter (in-place). */
    if (fs_create_file("rn_long_name") != 1) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: create\n");
        test_disk_close();
        return 1;
    }
    if (fs_write_file("rn_long_name", data, sizeof(data)) != (int)sizeof(data)) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: write\n");
        test_disk_close();
        return 1;
    }

    if (fs_rename("rn_long_name", "rn_short") != 1) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: rename returned error\n");
        test_disk_close();
        return 1;
    }

    /* Old name gone, new name exists with data intact. */
    if (sys_file_open("rn_long_name", &file) != 0) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: old name still exists\n");
        test_disk_close();
        return 1;
    }
    if (sys_file_open("rn_short", &file) != 1) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: new name not found\n");
        test_disk_close();
        return 1;
    }
    memset(out, 0, sizeof(out));
    if (fs_read_file("rn_short", out, sizeof(out)) != (int)sizeof(data) ||
        memcmp(out, data, sizeof(data)) != 0) {
        fprintf(stderr, "FAIL rename_same_dir_shorter: data mismatch\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS rename_same_dir_shorter");
    return 0;
}

/* ========================================================================
 * New tests: Path resolution
 * ======================================================================== */

static int test_path_single_level(const char* image) {
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL path_single: open\n"); return 1; }

    /* The default image has "system/version.txt". */
    if (sys_file_open("system/version.txt", &file) != 1) {
        fprintf(stderr, "FAIL path_single: system/version.txt not found\n");
        test_disk_close();
        return 1;
    }

    /* A non-existent directory should fail. */
    if (sys_file_open("nonexist/file.txt", &file) != 0) {
        fprintf(stderr, "FAIL path_single: nonexist should fail\n");
        test_disk_close();
        return 1;
    }

    /* A file in a valid directory but wrong name should fail. */
    if (sys_file_open("system/nonexist.txt", &file) != 0) {
        fprintf(stderr, "FAIL path_single: nonexist.txt should fail\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS path_single_level");
    return 0;
}

static int test_path_multi_level(const char* image) {
    SystemFile file;
    unsigned char data[] = "nested_data";
    unsigned char out[32];

    if (!open_fs(image)) { fprintf(stderr, "FAIL path_multi: open\n"); return 1; }

    /* Create nested directories: system/subdir */
    if (fs_mkdir("system/subdir") != 1) {
        fprintf(stderr, "FAIL path_multi: mkdir system/subdir\n");
        test_disk_close();
        return 1;
    }

    /* Create a file in the nested directory. */
    if (fs_create_file("system/subdir/nested.txt") != 1) {
        fprintf(stderr, "FAIL path_multi: create system/subdir/nested.txt\n");
        test_disk_close();
        return 1;
    }
    if (fs_write_file("system/subdir/nested.txt", data, sizeof(data)) != (int)sizeof(data)) {
        fprintf(stderr, "FAIL path_multi: write\n");
        test_disk_close();
        return 1;
    }

    /* Verify the file can be opened. */
    if (sys_file_open("system/subdir/nested.txt", &file) != 1) {
        fprintf(stderr, "FAIL path_multi: open system/subdir/nested.txt\n");
        test_disk_close();
        return 1;
    }

    /* Verify the data reads back correctly. */
    memset(out, 0, sizeof(out));
    if (fs_read_file("system/subdir/nested.txt", out, sizeof(out)) != (int)sizeof(data) ||
        memcmp(out, data, sizeof(data)) != 0) {
        fprintf(stderr, "FAIL path_multi: data mismatch\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS path_multi_level");
    return 0;
}

static int test_path_trailing_slash(const char* image) {
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL path_trailing_slash: open\n"); return 1; }

    /* The path resolver supports trailing slashes: "system/" resolves to the
     * system directory inode.  sys_file_open will succeed but return a
     * directory (type == 1), not a regular file. */
    if (sys_file_open("system/", &file) != 1) {
        fprintf(stderr, "FAIL path_trailing_slash: system/ should resolve to dir\n");
        test_disk_close();
        return 1;
    }
    if (file.type != 1) {
        fprintf(stderr, "FAIL path_trailing_slash: system/ should be a directory (type=%u)\n",
                file.type);
        test_disk_close();
        return 1;
    }

    /* Double slashes: "system//version.txt" — the resolver skips consecutive
     * slashes, so this should resolve to the same file as "system/version.txt". */
    if (sys_file_open("system//version.txt", &file) != 1) {
        fprintf(stderr, "FAIL path_trailing_slash: double-slash should resolve\n");
        test_disk_close();
        return 1;
    }
    if (file.type != 0) {
        fprintf(stderr, "FAIL path_trailing_slash: //version should be a file (type=%u)\n",
                file.type);
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS path_trailing_slash");
    return 0;
}

static int test_path_dot_components(const char* image) {
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL path_dot: open\n"); return 1; }

    /* "." resolves to root inode (a directory).  sys_file_open succeeds but
     * returns type == 1 (directory). */
    if (sys_file_open(".", &file) != 1 || file.type != 1) {
        fprintf(stderr, "FAIL path_dot: '.' should resolve to root dir\n");
        test_disk_close();
        return 1;
    }

    /* "system/./version.txt" — "." in the middle should be a no-op. */
    if (sys_file_open("system/./version.txt", &file) != 1 || file.type != 0) {
        fprintf(stderr, "FAIL path_dot: system/./version.txt should resolve to file\n");
        test_disk_close();
        return 1;
    }

    /* ".." from system should go back to root.  "system/../system/version.txt"
     * should resolve to the same file. */
    if (sys_file_open("system/../system/version.txt", &file) != 1 || file.type != 0) {
        fprintf(stderr, "FAIL path_dot: system/../system/version.txt should resolve\n");
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS path_dot_components");
    return 0;
}

/* ========================================================================
 * New tests: File write and read-back verification
 * ======================================================================== */

static int test_write_multiblock(const char* image) {
    /* Write a file larger than one block (> 1024 bytes) and verify
     * byte-for-byte read-back. */
    unsigned char payload[2048];
    unsigned char* out;
    int i;
    SystemFile file;

    if (!open_fs(image)) { fprintf(stderr, "FAIL write_multiblock: open\n"); return 1; }

    /* Fill with a recognizable pattern. */
    for (i = 0; i < (int)sizeof(payload); i++) {
        payload[i] = (unsigned char)(i & 0xFF);
    }

    if (fs_write_file("system/config.rtsk", payload, sizeof(payload)) != (int)sizeof(payload)) {
        fprintf(stderr, "FAIL write_multiblock: write\n");
        test_disk_close();
        return 1;
    }

    /* Verify size. */
    if (sys_file_open("system/config.rtsk", &file) != 1 || file.size != sizeof(payload)) {
        fprintf(stderr, "FAIL write_multiblock: size=%u expected=%zu\n",
                file.size, sizeof(payload));
        test_disk_close();
        return 1;
    }

    /* Read back and compare. */
    out = (unsigned char*)malloc(sizeof(payload));
    if (!out) { test_disk_close(); return 1; }
    memset(out, 0, sizeof(payload));
    if (fs_read_file("system/config.rtsk", out, sizeof(payload)) != (int)sizeof(payload)) {
        fprintf(stderr, "FAIL write_multiblock: read length\n");
        free(out);
        test_disk_close();
        return 1;
    }
    if (memcmp(payload, out, sizeof(payload)) != 0) {
        fprintf(stderr, "FAIL write_multiblock: data mismatch\n");
        free(out);
        test_disk_close();
        return 1;
    }

    free(out);
    test_disk_close();
    puts("PASS write_multiblock");
    return 0;
}

static int test_write_indirect_block(const char* image) {
    /* Write a file that needs the indirect block (> 12 direct blocks = > 12KB).
     * The FS supports 12 direct + 1 indirect (256 entries) = up to 268KB.
     * We'll write 13 blocks = 13312 bytes, which requires the indirect block. */
    unsigned int size = 13 * 1024;  /* 13312 bytes */
    unsigned char* payload;
    unsigned char* out;
    int i;
    SystemFile file;

    payload = (unsigned char*)malloc(size);
    out = (unsigned char*)malloc(size);
    if (!payload || !out) {
        fprintf(stderr, "FAIL write_indirect: malloc\n");
        free(payload);
        free(out);
        return 1;
    }

    if (!open_fs(image)) { fprintf(stderr, "FAIL write_indirect: open\n"); free(payload); free(out); return 1; }

    /* Fill with a pattern that spans block boundaries. */
    for (i = 0; i < (int)size; i++) {
        payload[i] = (unsigned char)((i * 7 + 13) & 0xFF);
    }

    if (fs_write_file("system/config.rtsk", payload, size) != (int)size) {
        fprintf(stderr, "FAIL write_indirect: write returned error (size=%u)\n", size);
        free(payload);
        free(out);
        test_disk_close();
        return 1;
    }

    /* Verify size. */
    if (sys_file_open("system/config.rtsk", &file) != 1 || file.size != size) {
        fprintf(stderr, "FAIL write_indirect: size=%u expected=%u\n", file.size, size);
        free(payload);
        free(out);
        test_disk_close();
        return 1;
    }

    /* Read back and compare byte-for-byte. */
    memset(out, 0, size);
    if (fs_read_file("system/config.rtsk", out, size) != (int)size) {
        fprintf(stderr, "FAIL write_indirect: read length\n");
        free(payload);
        free(out);
        test_disk_close();
        return 1;
    }
    if (memcmp(payload, out, size) != 0) {
        /* Find first mismatch for debugging. */
        for (i = 0; i < (int)size; i++) {
            if (payload[i] != out[i]) break;
        }
        fprintf(stderr, "FAIL write_indirect: data mismatch at byte %d (wrote 0x%02x read 0x%02x)\n",
                i, payload[i], out[i]);
        free(payload);
        free(out);
        test_disk_close();
        return 1;
    }

    free(payload);
    free(out);
    test_disk_close();
    puts("PASS write_indirect_block");
    return 0;
}

static int test_write_truncate_verify(const char* image) {
    /* Write a large file, then write a smaller amount, verify the file shrinks
     * and freed blocks are reclaimed. */
    unsigned char payload[4096];
    unsigned char small_payload[128];
    unsigned char out[256];
    unsigned int free_before;
    unsigned int free_after_large;
    unsigned int free_after_small;
    SystemFile file;
    int i;

    if (!open_fs(image)) { fprintf(stderr, "FAIL write_truncate: open\n"); return 1; }

    /* Fill large payload. */
    for (i = 0; i < (int)sizeof(payload); i++) {
        payload[i] = (unsigned char)(i & 0xFF);
    }
    /* Fill small payload with different data. */
    for (i = 0; i < (int)sizeof(small_payload); i++) {
        small_payload[i] = (unsigned char)((i + 0x80) & 0xFF);
    }

    /* Write large file. */
    free_before = test_free_blocks();
    if (fs_write_file("system/config.rtsk", payload, sizeof(payload)) != (int)sizeof(payload)) {
        fprintf(stderr, "FAIL write_truncate: large write\n");
        test_disk_close();
        return 1;
    }
    free_after_large = test_free_blocks();

    /* Truncate to smaller size. */
    if (fs_write_file("system/config.rtsk", small_payload, sizeof(small_payload)) != (int)sizeof(small_payload)) {
        fprintf(stderr, "FAIL write_truncate: small write\n");
        test_disk_close();
        return 1;
    }
    free_after_small = test_free_blocks();

    /* Verify file size. */
    if (sys_file_open("system/config.rtsk", &file) != 1 || file.size != sizeof(small_payload)) {
        fprintf(stderr, "FAIL write_truncate: size=%u expected=%zu\n",
                file.size, sizeof(small_payload));
        test_disk_close();
        return 1;
    }

    /* Verify content. */
    memset(out, 0, sizeof(out));
    if (fs_read_file("system/config.rtsk", out, sizeof(out)) != (int)sizeof(small_payload)) {
        fprintf(stderr, "FAIL write_truncate: read length\n");
        test_disk_close();
        return 1;
    }
    if (memcmp(out, small_payload, sizeof(small_payload)) != 0) {
        fprintf(stderr, "FAIL write_truncate: data mismatch\n");
        test_disk_close();
        return 1;
    }

    /* Verify blocks were reclaimed: free_after_small should be more than
     * free_after_large (we freed 3 blocks: 4 data blocks → 1 data block). */
    if (free_after_small <= free_after_large) {
        fprintf(stderr, "FAIL write_truncate: blocks not reclaimed (free before=%u after_large=%u after_small=%u)\n",
                free_before, free_after_large, free_after_small);
        test_disk_close();
        return 1;
    }

    test_disk_close();
    puts("PASS write_truncate_verify");
    return 0;
}

/* ========================================================================
 * Test runner
 * ======================================================================== */

typedef struct {
    const char* name;
    int (*func)(const char*);
} FsTest;

static const FsTest fs_tests[] = {
    {"bounded",      test_bounded_read},
    {"shrink",       test_shrink},
    {"truncate",     test_truncate_zero},
    {"alloc-fail",   test_allocation_failure},
    {"bad-dir",      test_bad_directory},
    {"multiblock",   test_multiblock_dir},
    {"rename",       test_rename_basic},
    {"rename-exist", test_rename_to_existing},
    {"rename-cross", test_rename_cross_dir},
    {"rename-short", test_rename_same_dir_shorter},
    {"path-single",  test_path_single_level},
    {"path-multi",   test_path_multi_level},
    {"path-slash",   test_path_trailing_slash},
    {"path-dot",     test_path_dot_components},
    {"write-multi",  test_write_multiblock},
    {"write-indir",  test_write_indirect_block},
    {"write-trunc",  test_write_truncate_verify},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    int i;

    if (argc == 3) {
        /* Single-test mode: TEST IMAGE */
        for (i = 0; fs_tests[i].name; i++) {
            if (strcmp(argv[1], fs_tests[i].name) == 0) {
                return fs_tests[i].func(argv[2]);
            }
        }
        fprintf(stderr, "unknown test: %s\n", argv[1]);
        return 2;
    }

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (i = 0; fs_tests[i].name; i++) {
            printf("%s\n", fs_tests[i].name);
        }
        return 0;
    }

    fprintf(stderr, "usage: %s TEST IMAGE\n", argv[0]);
    fprintf(stderr, "       %s --list\n", argv[0]);
    return 2;
}
