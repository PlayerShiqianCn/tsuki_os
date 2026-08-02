#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg.h"

static unsigned char* load_file(const char* path, int* out_size) {
    FILE* fp;
    long length;
    unsigned char* data;

    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    length = ftell(fp);
    if (length <= 0 || length > 0x7fffffffL || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    data = (unsigned char*)malloc((size_t)length);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_size = (int)length;
    return data;
}

static int find_marker(const unsigned char* data, int size, unsigned char wanted, int start) {
    int i;
    for (i = start; i + 3 < size; i++) {
        if (data[i] == 0xff && data[i + 1] == wanted) return i;
    }
    return -1;
}

static int test_valid(const char* path) {
    unsigned char* data;
    unsigned char* rgb;
    JpegInfo info;
    int size;
    size_t capacity;
    int ok;

    data = load_file(path, &size);
    if (!data || !jpeg_probe(data, size, &info)) return 1;
    capacity = (size_t)info.width * (size_t)info.height * 3u;
    rgb = (unsigned char*)malloc(capacity);
    if (!rgb) return 1;
    ok = jpeg_decode_rgb(data, size, rgb, (int)capacity, &info);
    free(rgb);
    free(data);
    if (!ok) return 1;
    printf("PASS valid_jpeg %dx%d progressive=%d\n", info.width, info.height, info.progressive);
    return 0;
}

static int test_huge_dimensions(const char* path) {
    unsigned char* data;
    unsigned char rgb[3] = {0};
    JpegInfo info;
    int size;
    int sof;
    int ok;

    data = load_file(path, &size);
    if (!data) return 1;
    sof = find_marker(data, size, 0xc2, 0);
    if (sof < 0) sof = find_marker(data, size, 0xc0, 0);
    if (sof < 0 || sof + 9 >= size) return 1;
    data[sof + 5] = 0xff;
    data[sof + 6] = 0xff;
    data[sof + 7] = 0xff;
    data[sof + 8] = 0xff;
    ok = jpeg_decode_rgb(data, size, rgb, (int)sizeof(rgb), &info);
    free(data);
    if (ok) {
        fprintf(stderr, "FAIL huge_dimensions accepted\n");
        return 1;
    }
    puts("PASS huge_dimensions rejected");
    return 0;
}

static int test_invalid_al(const char* path) {
    unsigned char* data;
    unsigned char* rgb;
    JpegInfo info;
    int size;
    int sos = -1;
    int search = 0;
    int ok;

    data = load_file(path, &size);
    if (!data || !jpeg_probe(data, size, &info)) return 1;
    while ((sos = find_marker(data, size, 0xda, search)) >= 0) {
        int seg_size;
        int count;
        int params;
        if (sos + 5 >= size) return 1;
        seg_size = ((int)data[sos + 2] << 8) | data[sos + 3];
        count = data[sos + 4];
        params = sos + 5 + count * 2;
        if (seg_size >= 6 && params + 2 < size && data[params] == 0 && data[params + 1] == 0) {
            data[params + 2] = 0x0f;
            break;
        }
        search = sos + 2;
    }
    if (sos < 0) return 1;
    rgb = (unsigned char*)malloc((size_t)info.width * (size_t)info.height * 3u);
    if (!rgb) return 1;
    ok = jpeg_decode_rgb(data, size, rgb, info.width * info.height * 3, &info);
    free(rgb);
    free(data);
    if (ok) {
        fprintf(stderr, "FAIL invalid_al accepted\n");
        return 1;
    }
    puts("PASS invalid_al rejected");
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s TEST JPEG\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "valid") == 0) return test_valid(argv[2]);
    if (strcmp(argv[1], "huge") == 0) return test_huge_dimensions(argv[2]);
    if (strcmp(argv[1], "invalid-al") == 0) return test_invalid_al(argv[2]);
    return 2;
}
