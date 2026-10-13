#ifndef JPEG_INTERNAL_H
#define JPEG_INTERNAL_H

/*
 * JPEG 解码器内部共享头 —— 仅限 jpeg 各翻译单元使用。
 *
 * 拆分布局：
 *   jpeg_core.c         位读取器、哈夫曼、DCT/色彩空间等原语
 *   jpeg_progressive.c  渐进式（SOF2）解码路径 + 系数存储
 *   jpeg.c              探测、基线（SOF0）解码、入口分发
 *
 * 公开 API 见 jpeg.h。
 */

#include "jpeg.h"

#define JPEG_MAX_COMPONENTS 3
#define JPEG_MAX_HUFF_SYMBOLS 256
#define JPEG_PROG_MAX_BLOCKS 512
#define JPEG_INT_MAX 2147483647
#define JPEG_INT_MIN (-2147483647 - 1)

typedef struct {
    unsigned char counts[16];
    unsigned char symbols[JPEG_MAX_HUFF_SYMBOLS];
    int min_code[17];
    int max_code[17];
    int val_ptr[17];
    int valid;
} JpegHuffTable;

typedef struct {
    int id;
    int h;
    int v;
    int tq;
    int td;
    int ta;
    int dc_pred;
} JpegComponent;

typedef struct {
    const unsigned char* data;
    int size;
    int pos;
    unsigned int bit_buf;
    int bit_count;
} JpegBitReader;

/* 定义于 jpeg_core.c */
extern const unsigned char zigzag_map[64];
extern const short idct_basis[8][8];

unsigned short read_be16(const unsigned char* p);
int is_sof_marker(unsigned char marker);
int next_marker(const unsigned char* data, int size, int* pos, unsigned char* out_marker, unsigned short* out_len);
void reset_huff_table(JpegHuffTable* table);
int build_huff_table(JpegHuffTable* table);
int huff_symbols_valid(int table_class, const unsigned char* symbols, int count);
int get_bits(JpegBitReader* br, int count, int* out_value);
void byte_align(JpegBitReader* br);
int receive_extend(JpegBitReader* br, int bits, int* out_value);
int huff_decode(JpegBitReader* br, const JpegHuffTable* table, int* out_symbol);
void dequantize_block(int* block, const unsigned short* qt);
int clamp_u8(int value);
int add_dc_diff(JpegComponent* comp, int diff);
int scale_to_short(int value, int shift, short* out_value);
int required_rgb_capacity(int width, int height, int* out_required);
int idct_block(const int* coeff, unsigned char* out_pixels);
void ycbcr_to_rgb(int y, int cb, int cr, unsigned char* out_rgb);

/* 定义于 jpeg_progressive.c，由 jpeg.c 分发调用 */
int jpeg_decode_progressive_rgb(const unsigned char* data, int size, unsigned char* out_rgb, int out_capacity, JpegInfo* out_info);

/* 诊断错误码（定义于 jpeg.c，jpeg_debug_last_error 读取） */
extern int jpeg_last_error_code;

#endif
