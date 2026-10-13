// jpeg_core.c — JPEG 解码原语：字节序读取、marker 扫描、哈夫曼表、
// 位读取器、反量化、IDCT、YCbCr→RGB。共享类型见 jpeg_internal.h。

#include "jpeg_internal.h"

const unsigned char zigzag_map[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

const short idct_basis[8][8] = {
    {91, 126, 118, 106, 91, 71, 49, 25},
    {91, 106, 49, -25, -91, -126, -118, -71},
    {91, 71, -49, -126, -91, 25, 118, 106},
    {91, 25, -118, -71, 91, 106, -49, -126},
    {91, -25, -118, 71, 91, -106, -49, 126},
    {91, -71, -49, 126, -91, -25, 118, -106},
    {91, -106, 49, 25, -91, 126, -118, 71},
    {91, -126, 118, -106, 91, -71, 49, -25}
};

unsigned short read_be16(const unsigned char* p) {
    return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

int is_sof_marker(unsigned char marker) {
    if (marker >= 0xC0 && marker <= 0xCF) {
        if (marker == 0xC4 || marker == 0xC8 || marker == 0xCC) return 0;
        return 1;
    }
    return 0;
}

void reset_huff_table(JpegHuffTable* table) {
    if (!table) return;
    for (int i = 0; i < 16; i++) {
        table->counts[i] = 0;
        table->min_code[i + 1] = -1;
        table->max_code[i + 1] = -1;
        table->val_ptr[i + 1] = 0;
    }
    table->valid = 0;
}

int build_huff_table(JpegHuffTable* table) {
    int code = 0;
    int k = 0;
    int codes_left = 1;

    table->min_code[0] = -1;
    table->max_code[0] = -1;
    table->val_ptr[0] = 0;
    table->valid = 0;

    for (int len = 1; len <= 16; len++) {
        int count = table->counts[len - 1];
        codes_left = (codes_left << 1) - count;
        if (codes_left < 0) return 0;

        if (count == 0) {
            table->min_code[len] = -1;
            table->max_code[len] = -1;
            table->val_ptr[len] = k;
        } else {
            table->min_code[len] = code;
            table->val_ptr[len] = k;
            code += count - 1;
            table->max_code[len] = code;
            code++;
            k += count;
        }
        code <<= 1;
    }

    if (k == 0) return 0;
    table->valid = 1;
    return 1;
}

int huff_symbols_valid(int table_class, const unsigned char* symbols, int count) {
    for (int i = 0; i < count; i++) {
        int symbol = symbols[i];
        if (table_class == 0) {
            if (symbol > 11) return 0;
        } else {
            if ((symbol & 0x0F) > 10) return 0;
        }
    }
    return 1;
}

int next_marker(const unsigned char* data, int size, int* pos, unsigned char* out_marker, unsigned short* out_len) {
    while (*pos < size && data[*pos] != 0xFF) (*pos)++;
    if (*pos >= size) return 0;

    while (*pos < size && data[*pos] == 0xFF) (*pos)++;
    if (*pos >= size) return 0;

    *out_marker = data[(*pos)++];
    if (*out_marker == 0xD8 || *out_marker == 0xD9 ||
        (*out_marker >= 0xD0 && *out_marker <= 0xD7) ||
        *out_marker == 0x01) {
        *out_len = 0;
        return 1;
    }

    if (*pos + 1 >= size) return 0;
    *out_len = read_be16(data + *pos);
    *pos += 2;
    if (*out_len < 2) return 0;
    if (*pos + (int)(*out_len) - 2 > size) return 0;
    return 1;
}

static int fill_bits(JpegBitReader* br, int need_bits) {
    while (br->bit_count < need_bits) {
        unsigned char byte;
        if (br->pos >= br->size) return 0;
        byte = br->data[br->pos++];
        if (byte == 0xFF) {
            if (br->pos >= br->size) return 0;
            {
                unsigned char stuffed = br->data[br->pos++];
                if (stuffed != 0x00) {
                    return 0;
                }
            }
        }
        br->bit_buf = (br->bit_buf << 8) | (unsigned int)byte;
        br->bit_count += 8;
    }
    return 1;
}

int get_bits(JpegBitReader* br, int count, int* out_value) {
    if (!br || !out_value || count < 0 || count > 16) return 0;
    if (count == 0) {
        *out_value = 0;
        return 1;
    }
    if (!fill_bits(br, count)) return 0;
    *out_value = (int)((br->bit_buf >> (br->bit_count - count)) & ((1u << count) - 1u));
    br->bit_count -= count;
    return 1;
}

void byte_align(JpegBitReader* br) {
    br->bit_count &= ~7;
}

int receive_extend(JpegBitReader* br, int bits, int* out_value) {
    int value;
    if (!get_bits(br, bits, &value)) return 0;
    if (bits > 0) {
        int vt = 1 << (bits - 1);
        if (value < vt) {
            value -= (1 << bits) - 1;
        }
    }
    *out_value = value;
    return 1;
}

int huff_decode(JpegBitReader* br, const JpegHuffTable* table, int* out_symbol) {
    int code = 0;

    if (!table || !table->valid) return 0;
    for (int len = 1; len <= 16; len++) {
        int bit;
        if (!get_bits(br, 1, &bit)) return 0;
        code = (code << 1) | bit;
        if (table->max_code[len] >= 0 && code <= table->max_code[len]) {
            int idx = table->val_ptr[len] + code - table->min_code[len];
            if (idx < 0 || idx >= JPEG_MAX_HUFF_SYMBOLS) return 0;
            *out_symbol = table->symbols[idx];
            return 1;
        }
    }
    return 0;
}

void dequantize_block(int* block, const unsigned short* qt) {
    for (int i = 0; i < 64; i++) {
        block[i] *= (int)qt[i];
    }
}

int clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

int add_dc_diff(JpegComponent* comp, int diff) {
    if (!comp) return 0;
    if (diff > 0 && comp->dc_pred > 32767 - diff) return 0;
    if (diff < 0 && comp->dc_pred < -32768 - diff) return 0;
    comp->dc_pred += diff;
    return 1;
}

int scale_to_short(int value, int shift, short* out_value) {
    int scale;
    int scaled;

    if (!out_value || shift < 0 || shift > 13) return 0;
    scale = 1 << shift;
    if (value > 32767 / scale || value < -32768 / scale) return 0;
    scaled = value * scale;
    *out_value = (short)scaled;
    return 1;
}

int required_rgb_capacity(int width, int height, int* out_required) {
    int row_bytes;

    if (!out_required || width <= 0 || height <= 0) return 0;
    if (width > JPEG_INT_MAX / 3) return 0;
    row_bytes = width * 3;
    if (height > JPEG_INT_MAX / row_bytes) return 0;
    *out_required = row_bytes * height;
    return 1;
}

static int add_idct_term(int* sum, int value, int basis) {
    int term = value * basis;
    if (term > 0 && *sum > JPEG_INT_MAX - term) return 0;
    if (term < 0 && *sum < JPEG_INT_MIN - term) return 0;
    *sum += term;
    return 1;
}

int idct_block(const int* coeff, unsigned char* out_pixels) {
    int temp[64];

    for (int row = 0; row < 8; row++) {
        for (int x = 0; x < 8; x++) {
            int sum = 0;
            for (int u = 0; u < 8; u++) {
                if (!add_idct_term(&sum, coeff[row * 8 + u], (int)idct_basis[x][u])) return 0;
            }
            if (sum > JPEG_INT_MAX - 128) return 0;
            temp[row * 8 + x] = (sum + 128) >> 8;
        }
    }

    for (int col = 0; col < 8; col++) {
        for (int y = 0; y < 8; y++) {
            int sum = 0;
            for (int v = 0; v < 8; v++) {
                if (!add_idct_term(&sum, temp[v * 8 + col], (int)idct_basis[y][v])) return 0;
            }
            if (sum > JPEG_INT_MAX - 128) return 0;
            out_pixels[y * 8 + col] = (unsigned char)clamp_u8(((sum + 128) >> 8) + 128);
        }
    }
    return 1;
}

void ycbcr_to_rgb(int y, int cb, int cr, unsigned char* out_rgb) {
    int ccb = cb - 128;
    int ccr = cr - 128;
    int r = y + ((359 * ccr) >> 8);
    int g = y - ((88 * ccb + 183 * ccr) >> 8);
    int b = y + ((454 * ccb) >> 8);

    out_rgb[0] = (unsigned char)clamp_u8(r);
    out_rgb[1] = (unsigned char)clamp_u8(g);
    out_rgb[2] = (unsigned char)clamp_u8(b);
}
