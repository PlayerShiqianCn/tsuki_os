#include "jpeg.h"

#define JPEG_MAX_COMPONENTS 3
#define JPEG_MAX_HUFF_SYMBOLS 256
#define JPEG_PROG_MAX_BLOCKS 512

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

static const unsigned char zigzag_map[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const short idct_basis[8][8] = {
    {91, 126, 118, 106, 91, 71, 49, 25},
    {91, 106, 49, -25, -91, -126, -118, -71},
    {91, 71, -49, -126, -91, 25, 118, 106},
    {91, 25, -118, -71, 91, 106, -49, -126},
    {91, -25, -118, 71, 91, -106, -49, 126},
    {91, -71, -49, 126, -91, -25, 118, -106},
    {91, -106, 49, 25, -91, 126, -118, 71},
    {91, -126, 118, -106, 91, -71, 49, -25}
};

static short prog_coeffs[JPEG_PROG_MAX_BLOCKS * 64];
static int prog_comp_offset[JPEG_MAX_COMPONENTS];
static int prog_comp_store_x[JPEG_MAX_COMPONENTS];
static int prog_comp_store_y[JPEG_MAX_COMPONENTS];
static int prog_comp_blocks_x[JPEG_MAX_COMPONENTS];
static int prog_comp_blocks_y[JPEG_MAX_COMPONENTS];
static int jpeg_last_error_code = 0;

static unsigned short read_be16(const unsigned char* p) {
    return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

static int is_sof_marker(unsigned char marker) {
    if (marker >= 0xC0 && marker <= 0xCF) {
        if (marker == 0xC4 || marker == 0xC8 || marker == 0xCC) return 0;
        return 1;
    }
    return 0;
}

static void reset_huff_table(JpegHuffTable* table) {
    if (!table) return;
    for (int i = 0; i < 16; i++) {
        table->counts[i] = 0;
        table->min_code[i + 1] = -1;
        table->max_code[i + 1] = -1;
        table->val_ptr[i + 1] = 0;
    }
    table->valid = 0;
}

static void build_huff_table(JpegHuffTable* table) {
    int code = 0;
    int k = 0;

    table->min_code[0] = -1;
    table->max_code[0] = -1;
    table->val_ptr[0] = 0;

    for (int len = 1; len <= 16; len++) {
        int count = table->counts[len - 1];
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
    table->valid = 1;
}

static int next_marker(const unsigned char* data, int size, int* pos, unsigned char* out_marker, unsigned short* out_len) {
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

static int get_bits(JpegBitReader* br, int count, int* out_value) {
    if (count == 0) {
        *out_value = 0;
        return 1;
    }
    if (!fill_bits(br, count)) return 0;
    *out_value = (int)((br->bit_buf >> (br->bit_count - count)) & ((1u << count) - 1u));
    br->bit_count -= count;
    return 1;
}

static int get_bit(JpegBitReader* br, int* out_bit) {
    return get_bits(br, 1, out_bit);
}

static void byte_align(JpegBitReader* br) {
    br->bit_count &= ~7;
}

static int receive_extend(JpegBitReader* br, int bits, int* out_value) {
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

static int huff_decode(JpegBitReader* br, const JpegHuffTable* table, int* out_symbol) {
    int code = 0;

    if (!table || !table->valid) return 0;
    for (int len = 1; len <= 16; len++) {
        int bit;
        if (!get_bit(br, &bit)) return 0;
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

static void dequantize_block(int* block, const unsigned short* qt) {
    for (int i = 0; i < 64; i++) {
        block[i] *= (int)qt[i];
    }
}

static int clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static void idct_block(const int* coeff, unsigned char* out_pixels) {
    int temp[64];

    for (int row = 0; row < 8; row++) {
        for (int x = 0; x < 8; x++) {
            int sum = 0;
            for (int u = 0; u < 8; u++) {
                sum += coeff[row * 8 + u] * (int)idct_basis[x][u];
            }
            temp[row * 8 + x] = (sum + 128) >> 8;
        }
    }

    for (int col = 0; col < 8; col++) {
        for (int y = 0; y < 8; y++) {
            int sum = 0;
            for (int v = 0; v < 8; v++) {
                sum += temp[v * 8 + col] * (int)idct_basis[y][v];
            }
            out_pixels[y * 8 + col] = (unsigned char)clamp_u8(((sum + 128) >> 8) + 128);
        }
    }
}

static void ycbcr_to_rgb(int y, int cb, int cr, unsigned char* out_rgb) {
    int ccb = cb - 128;
    int ccr = cr - 128;
    int r = y + ((359 * ccr) >> 8);
    int g = y - ((88 * ccb + 183 * ccr) >> 8);
    int b = y + ((454 * ccb) >> 8);

    out_rgb[0] = (unsigned char)clamp_u8(r);
    out_rgb[1] = (unsigned char)clamp_u8(g);
    out_rgb[2] = (unsigned char)clamp_u8(b);
}

static short* prog_block_ptr(int comp_index, int bx, int by) {
    int block_index = prog_comp_offset[comp_index] + by * prog_comp_store_x[comp_index] + bx;
    return &prog_coeffs[block_index * 64];
}

static int prog_setup_blocks(const JpegComponent* comps, int comp_count, int width, int height, int max_h, int max_v) {
    int mcu_cols = (width + max_h * 8 - 1) / (max_h * 8);
    int mcu_rows = (height + max_v * 8 - 1) / (max_v * 8);
    int total_blocks = 0;

    for (int i = 0; i < comp_count; i++) {
        prog_comp_offset[i] = total_blocks;
        prog_comp_store_x[i] = mcu_cols * comps[i].h;
        prog_comp_store_y[i] = mcu_rows * comps[i].v;
        prog_comp_blocks_x[i] = (width * comps[i].h + max_h * 8 - 1) / (max_h * 8);
        prog_comp_blocks_y[i] = (height * comps[i].v + max_v * 8 - 1) / (max_v * 8);
        total_blocks += prog_comp_store_x[i] * prog_comp_store_y[i];
    }

    if (total_blocks > JPEG_PROG_MAX_BLOCKS) return 0;
    for (int i = 0; i < total_blocks * 64; i++) {
        prog_coeffs[i] = 0;
    }
    return 1;
}

static int prog_decode_dc_scan(const JpegComponent* frame_comps,
                               JpegComponent* const* scan_comps, int scan_comp_count,
                               const JpegHuffTable* dc_tables,
                               int width, int height, int max_h, int max_v, int al,
                               JpegBitReader* br) {
    int interleaved = (scan_comp_count > 1);

    if (interleaved) {
        int mcu_cols = (width + max_h * 8 - 1) / (max_h * 8);
        int mcu_rows = (height + max_v * 8 - 1) / (max_v * 8);

        for (int my = 0; my < mcu_rows; my++) {
            for (int mx = 0; mx < mcu_cols; mx++) {
                for (int sci = 0; sci < scan_comp_count; sci++) {
                    JpegComponent* comp = scan_comps[sci];
                    int frame_index = (int)(comp - frame_comps);
                    for (int vy = 0; vy < comp->v; vy++) {
                        for (int hx = 0; hx < comp->h; hx++) {
                            int symbol;
                            int diff;
                            short* block = prog_block_ptr(frame_index, mx * comp->h + hx, my * comp->v + vy);
                            if (!huff_decode(br, &dc_tables[comp->td], &symbol)) return 0;
                            if (!receive_extend(br, symbol, &diff)) return 0;
                            comp->dc_pred += diff;
                            block[0] = (short)(comp->dc_pred << al);
                        }
                    }
                }
            }
        }
        return 1;
    }

    {
        JpegComponent* comp = scan_comps[0];
        int frame_index = (int)(comp - frame_comps);
        int blocks_x = prog_comp_blocks_x[frame_index];
        int blocks_y = prog_comp_blocks_y[frame_index];

        for (int by = 0; by < blocks_y; by++) {
            for (int bx = 0; bx < blocks_x; bx++) {
                int symbol;
                int diff;
                short* block = prog_block_ptr(frame_index, bx, by);
                if (!huff_decode(br, &dc_tables[comp->td], &symbol)) return 0;
                if (!receive_extend(br, symbol, &diff)) return 0;
                comp->dc_pred += diff;
                block[0] = (short)(comp->dc_pred << al);
            }
        }
    }

    return 1;
}

static int prog_decode_ac_scan(const JpegComponent* frame_comps,
                               JpegComponent* const* scan_comps, const JpegHuffTable* ac_tables,
                               int ss, int se, int al, JpegBitReader* br) {
    int eobrun = 0;
    JpegComponent* comp = scan_comps[0];
    int frame_index = (int)(comp - frame_comps);
    int blocks_x = prog_comp_blocks_x[frame_index];
    int blocks_y = prog_comp_blocks_y[frame_index];

    for (int by = 0; by < blocks_y; by++) {
        for (int bx = 0; bx < blocks_x; bx++) {
            short* block = prog_block_ptr(frame_index, bx, by);
            int k = ss;

            if (eobrun > 0) {
                eobrun--;
                continue;
            }

            while (k <= se) {
                int symbol;
                int run;
                int size_bits;
                int value;

                if (!huff_decode(br, &ac_tables[comp->ta], &symbol)) return 0;
                run = (symbol >> 4) & 0x0F;
                size_bits = symbol & 0x0F;

                if (size_bits == 0) {
                    if (run == 15) {
                        k += 16;
                        continue;
                    }

                    eobrun = 1 << run;
                    if (run > 0) {
                        int extra;
                        if (!get_bits(br, run, &extra)) return 0;
                        eobrun += extra;
                    }
                    eobrun--;
                    break;
                }

                k += run;
                if (k > se || k >= 64) return 0;
                if (!receive_extend(br, size_bits, &value)) return 0;
                block[zigzag_map[k]] = (short)(value << al);
                k++;
            }
        }
    }

    return 1;
}

static int prog_output_rgb(const JpegComponent* comps, int comp_count,
                           const unsigned short qt[4][64],
                           int width, int height, int max_h, int max_v,
                           unsigned char* out_rgb) {
    unsigned char mcu_pixels[JPEG_MAX_COMPONENTS][16 * 16];
    int mcu_cols = (width + max_h * 8 - 1) / (max_h * 8);
    int mcu_rows = (height + max_v * 8 - 1) / (max_v * 8);

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int ci = 0; ci < comp_count; ci++) {
                const JpegComponent* comp = &comps[ci];
                int stride = comp->h * 8;
                int blocks = comp->h * comp->v;

                for (int i = 0; i < stride * comp->v * 8; i++) {
                    mcu_pixels[ci][i] = 0;
                }

                for (int block_idx = 0; block_idx < blocks; block_idx++) {
                    int block[64];
                    unsigned char spatial[64];
                    int bx = block_idx % comp->h;
                    int by = block_idx / comp->h;
                    short* coeff = prog_block_ptr(ci, mx * comp->h + bx, my * comp->v + by);

                    for (int i = 0; i < 64; i++) {
                        block[i] = (int)coeff[i];
                    }

                    dequantize_block(block, qt[comp->tq]);
                    idct_block(block, spatial);

                    for (int py = 0; py < 8; py++) {
                        for (int px = 0; px < 8; px++) {
                            int dx = bx * 8 + px;
                            int dy = by * 8 + py;
                            mcu_pixels[ci][dy * stride + dx] = spatial[py * 8 + px];
                        }
                    }
                }
            }

            for (int py = 0; py < max_v * 8; py++) {
                int iy = my * max_v * 8 + py;
                if (iy >= height) continue;
                for (int px = 0; px < max_h * 8; px++) {
                    int ix = mx * max_h * 8 + px;
                    unsigned char rgb[3];
                    if (ix >= width) continue;

                    if (comp_count == 1) {
                        int yv = mcu_pixels[0][py * (comps[0].h * 8) + px];
                        rgb[0] = (unsigned char)yv;
                        rgb[1] = (unsigned char)yv;
                        rgb[2] = (unsigned char)yv;
                    } else if (comp_count == 3) {
                        int values[3];
                        for (int ci = 0; ci < 3; ci++) {
                            int sx = (px * comps[ci].h) / max_h;
                            int sy = (py * comps[ci].v) / max_v;
                            int stride = comps[ci].h * 8;
                            values[ci] = (int)mcu_pixels[ci][sy * stride + sx];
                        }
                        ycbcr_to_rgb(values[0], values[1], values[2], rgb);
                    } else {
                        return 0;
                    }

                    {
                        int out_idx = (iy * width + ix) * 3;
                        out_rgb[out_idx] = rgb[0];
                        out_rgb[out_idx + 1] = rgb[1];
                        out_rgb[out_idx + 2] = rgb[2];
                    }
                }
            }
        }
    }

    return 1;
}

int jpeg_probe(const unsigned char* data, int size, JpegInfo* out_info) {
    int pos = 0;

    if (!data || size < 4 || !out_info) return 0;
    if (data[0] != 0xFF || data[1] != 0xD8) return 0;

    out_info->width = 0;
    out_info->height = 0;
    out_info->components = 0;
    out_info->progressive = 0;
    pos = 2;

    while (pos < size) {
        unsigned char marker;
        unsigned short seg_len;

        if (!next_marker(data, size, &pos, &marker, &seg_len)) return 0;
        if (marker == 0xD9 || marker == 0xDA) break;
        if (seg_len == 0) continue;

        if (is_sof_marker(marker)) {
            const unsigned char* seg = data + pos;
            if (seg_len < 8) return 0;
            if (seg[0] != 8) return 0;

            out_info->height = (int)read_be16(seg + 1);
            out_info->width = (int)read_be16(seg + 3);
            out_info->components = (int)seg[5];
            out_info->progressive = (marker == 0xC2) ? 1 : 0;

            if (out_info->width <= 0 || out_info->height <= 0 || out_info->components <= 0) {
                return 0;
            }
            return 1;
        }

        pos += (int)seg_len - 2;
    }

    return 0;
}

static int jpeg_decode_progressive_rgb(const unsigned char* data, int size, unsigned char* out_rgb, int out_capacity, JpegInfo* out_info) {
    unsigned short qt[4][64];
    JpegHuffTable dc_tables[4];
    JpegHuffTable ac_tables[4];
    JpegComponent comps[JPEG_MAX_COMPONENTS];
    int qt_valid[4];
    int comp_count = 0;
    int width = 0;
    int height = 0;
    int max_h = 1;
    int max_v = 1;
    int frame_ready = 0;
    int restart_interval = 0;
    int pos = 2;
    int scan_index = 0;

    if (!data || !out_rgb || !out_info || size < 4) return 0;
    if (data[0] != 0xFF || data[1] != 0xD8) return 0;
    jpeg_last_error_code = 100;

    for (int i = 0; i < 4; i++) {
        qt_valid[i] = 0;
        reset_huff_table(&dc_tables[i]);
        reset_huff_table(&ac_tables[i]);
        for (int j = 0; j < 64; j++) qt[i][j] = 0;
    }
    for (int i = 0; i < JPEG_MAX_COMPONENTS; i++) {
        comps[i].id = 0;
        comps[i].h = 0;
        comps[i].v = 0;
        comps[i].tq = 0;
        comps[i].td = 0;
        comps[i].ta = 0;
        comps[i].dc_pred = 0;
    }

    while (pos < size) {
        unsigned char marker;
        unsigned short seg_len;
        const unsigned char* seg;
        int seg_size;

        if (!next_marker(data, size, &pos, &marker, &seg_len)) return 0;
        if (marker == 0xD9) break;
        if (seg_len == 0) continue;

        seg = data + pos;
        seg_size = (int)seg_len - 2;

        if (marker == 0xDB) {
            int qpos = 0;
            while (qpos < seg_size) {
                int info;
                int precision;
                int table_id;
                if (qpos >= seg_size) return 0;
                info = seg[qpos++];
                precision = (info >> 4) & 0x0F;
                table_id = info & 0x0F;
                if (precision != 0 || table_id < 0 || table_id > 3) return 0;
                if (qpos + 64 > seg_size) return 0;
                for (int i = 0; i < 64; i++) {
                    qt[table_id][zigzag_map[i]] = (unsigned short)seg[qpos + i];
                }
                qt_valid[table_id] = 1;
                qpos += 64;
            }
        } else if (marker == 0xC2) {
            jpeg_last_error_code = 110;
            if (seg_size < 6) return 0;
            if (seg[0] != 8) return 0;
            height = (int)read_be16(seg + 1);
            width = (int)read_be16(seg + 3);
            comp_count = (int)seg[5];
            if (width <= 0 || height <= 0) return 0;
            if (comp_count < 1 || comp_count > JPEG_MAX_COMPONENTS) return 0;
            if (seg_size != 6 + comp_count * 3) return 0;

            max_h = 1;
            max_v = 1;
            for (int i = 0; i < comp_count; i++) {
                const unsigned char* c = seg + 6 + i * 3;
                comps[i].id = c[0];
                comps[i].h = (c[1] >> 4) & 0x0F;
                comps[i].v = c[1] & 0x0F;
                comps[i].tq = c[2];
                comps[i].td = 0;
                comps[i].ta = 0;
                comps[i].dc_pred = 0;
                if (comps[i].h < 1 || comps[i].h > 2 || comps[i].v < 1 || comps[i].v > 2) return 0;
                if (comps[i].tq < 0 || comps[i].tq > 3 || !qt_valid[comps[i].tq]) return 0;
                if (comps[i].h > max_h) max_h = comps[i].h;
                if (comps[i].v > max_v) max_v = comps[i].v;
            }
            if (!prog_setup_blocks(comps, comp_count, width, height, max_h, max_v)) return 0;
            frame_ready = 1;
        } else if (marker == 0xC0) {
            return 0;
        } else if (marker == 0xC4) {
            int hpos = 0;
            while (hpos < seg_size) {
                int info;
                int table_class;
                int table_id;
                int total = 0;
                JpegHuffTable* table;

                if (hpos >= seg_size) return 0;
                info = seg[hpos++];
                table_class = (info >> 4) & 0x0F;
                table_id = info & 0x0F;
                if (table_id < 0 || table_id > 3) return 0;
                if (hpos + 16 > seg_size) return 0;

                table = (table_class == 0) ? &dc_tables[table_id] : &ac_tables[table_id];
                if (table_class != 0 && table_class != 1) return 0;

                for (int i = 0; i < 16; i++) {
                    table->counts[i] = seg[hpos + i];
                    total += table->counts[i];
                }
                hpos += 16;
                if (total < 0 || total > JPEG_MAX_HUFF_SYMBOLS) return 0;
                if (hpos + total > seg_size) return 0;
                for (int i = 0; i < total; i++) {
                    table->symbols[i] = seg[hpos + i];
                }
                hpos += total;
                build_huff_table(table);
            }
        } else if (marker == 0xDD) {
            if (seg_size != 2) return 0;
            restart_interval = (int)read_be16(seg);
            if (restart_interval != 0) return 0;
        } else if (marker == 0xDA) {
            JpegComponent* scan_ptrs[JPEG_MAX_COMPONENTS];
            JpegBitReader br;
            int scan_comp_count;
            int ss, se, ah, al;

            if (!frame_ready) return 0;
            if (seg_size < 1) return 0;
            scan_comp_count = seg[0];
            if (scan_comp_count < 1 || scan_comp_count > comp_count) return 0;
            if (seg_size != 1 + scan_comp_count * 2 + 3) return 0;

            for (int i = 0; i < scan_comp_count; i++) {
                int scan_id = seg[1 + i * 2];
                int huff = seg[1 + i * 2 + 1];
                int found = -1;
                for (int j = 0; j < comp_count; j++) {
                    if (comps[j].id == scan_id) {
                        found = j;
                        break;
                    }
                }
                if (found < 0) return 0;
                comps[found].td = (huff >> 4) & 0x0F;
                comps[found].ta = huff & 0x0F;
                if (!dc_tables[comps[found].td].valid) return 0;
                if ((seg[1 + scan_comp_count * 2] != 0 || seg[1 + scan_comp_count * 2 + 1] != 0) &&
                    !ac_tables[comps[found].ta].valid) return 0;
                scan_ptrs[i] = &comps[found];
            }

            ss = seg[1 + scan_comp_count * 2];
            se = seg[1 + scan_comp_count * 2 + 1];
            ah = seg[1 + scan_comp_count * 2 + 2] >> 4;
            al = seg[1 + scan_comp_count * 2 + 2] & 0x0F;

            if (ah != 0) return 0;
            if (ss == 0 && se != 0) return 0;
            if (ss > se || se >= 64) return 0;
            if (ss != 0 && scan_comp_count != 1) return 0;

            br.data = data + pos + seg_size;
            br.size = size - (pos + seg_size);
            br.pos = 0;
            br.bit_buf = 0;
            br.bit_count = 0;

            jpeg_last_error_code = 200 + scan_index;
            if (ss == 0) {
                if (!prog_decode_dc_scan(comps, scan_ptrs, scan_comp_count, dc_tables,
                                         width, height, max_h, max_v, al, &br)) return 0;
            } else {
                if (!prog_decode_ac_scan(comps, scan_ptrs, ac_tables, ss, se, al, &br)) return 0;
            }
            scan_index++;

            br.bit_buf = 0;
            br.bit_count = 0;
            pos += seg_size + br.pos;
            continue;
        }

        pos += seg_size;
    }

    if (!frame_ready) return 0;
    if (out_capacity < width * height * 3) return 0;
    jpeg_last_error_code = 250;
    if (!prog_output_rgb(comps, comp_count, qt, width, height, max_h, max_v, out_rgb)) return 0;

    out_info->width = width;
    out_info->height = height;
    out_info->components = comp_count;
    out_info->progressive = 1;
    return 1;
}

static int jpeg_decode_baseline_rgb(const unsigned char* data, int size, unsigned char* out_rgb, int out_capacity, JpegInfo* out_info) {
    unsigned short qt[4][64];
    JpegHuffTable dc_tables[4];
    JpegHuffTable ac_tables[4];
    JpegComponent comps[JPEG_MAX_COMPONENTS];
    int qt_valid[4];
    int comp_count = 0;
    int restart_interval = 0;
    int max_h = 1;
    int max_v = 1;
    int width = 0;
    int height = 0;
    int scan_ready = 0;
    int sos_pos = 0;
    int sos_size = 0;
    int pos = 2;
    JpegBitReader br;
    unsigned char mcu_pixels[JPEG_MAX_COMPONENTS][16 * 16];
    int mcu_cols;
    int mcu_rows;
    int restarts_left = 0;
    int next_restart = 0;

    if (!data || !out_rgb || !out_info || size < 4) return 0;
    if (data[0] != 0xFF || data[1] != 0xD8) return 0;

    for (int i = 0; i < 4; i++) {
        qt_valid[i] = 0;
        reset_huff_table(&dc_tables[i]);
        reset_huff_table(&ac_tables[i]);
        for (int j = 0; j < 64; j++) qt[i][j] = 0;
    }
    for (int i = 0; i < JPEG_MAX_COMPONENTS; i++) {
        comps[i].id = 0;
        comps[i].h = 0;
        comps[i].v = 0;
        comps[i].tq = 0;
        comps[i].td = 0;
        comps[i].ta = 0;
        comps[i].dc_pred = 0;
    }

    while (pos < size) {
        unsigned char marker;
        unsigned short seg_len;
        const unsigned char* seg;
        int seg_size;

        if (!next_marker(data, size, &pos, &marker, &seg_len)) return 0;
        if (marker == 0xD9) return 0;
        if (marker == 0xDA) {
            scan_ready = 1;
            sos_pos = pos;
            sos_size = (int)seg_len - 2;
            break;
        }
        if (seg_len == 0) continue;

        seg = data + pos;
        seg_size = (int)seg_len - 2;

        if (marker == 0xDB) {
            int qpos = 0;
            while (qpos < seg_size) {
                int info;
                int precision;
                int table_id;
                if (qpos >= seg_size) return 0;
                info = seg[qpos++];
                precision = (info >> 4) & 0x0F;
                table_id = info & 0x0F;
                if (precision != 0 || table_id < 0 || table_id > 3) return 0;
                if (qpos + 64 > seg_size) return 0;
                for (int i = 0; i < 64; i++) {
                    qt[table_id][zigzag_map[i]] = (unsigned short)seg[qpos + i];
                }
                qt_valid[table_id] = 1;
                qpos += 64;
            }
        } else if (marker == 0xC0) {
            if (seg_size < 6) return 0;
            if (seg[0] != 8) return 0;
            height = (int)read_be16(seg + 1);
            width = (int)read_be16(seg + 3);
            comp_count = (int)seg[5];
            if (width <= 0 || height <= 0) return 0;
            if (comp_count < 1 || comp_count > JPEG_MAX_COMPONENTS) return 0;
            if (seg_size != 6 + comp_count * 3) return 0;

            max_h = 1;
            max_v = 1;
            for (int i = 0; i < comp_count; i++) {
                const unsigned char* c = seg + 6 + i * 3;
                comps[i].id = c[0];
                comps[i].h = (c[1] >> 4) & 0x0F;
                comps[i].v = c[1] & 0x0F;
                comps[i].tq = c[2];
                comps[i].td = 0;
                comps[i].ta = 0;
                comps[i].dc_pred = 0;
                if (comps[i].h < 1 || comps[i].h > 2 || comps[i].v < 1 || comps[i].v > 2) return 0;
                if (comps[i].tq < 0 || comps[i].tq > 3 || !qt_valid[comps[i].tq]) return 0;
                if (comps[i].h > max_h) max_h = comps[i].h;
                if (comps[i].v > max_v) max_v = comps[i].v;
            }
        } else if (marker == 0xC2) {
            return 0;
        } else if (marker == 0xC4) {
            int hpos = 0;
            while (hpos < seg_size) {
                int info;
                int table_class;
                int table_id;
                int total = 0;
                JpegHuffTable* table;

                if (hpos >= seg_size) return 0;
                info = seg[hpos++];
                table_class = (info >> 4) & 0x0F;
                table_id = info & 0x0F;
                if (table_id < 0 || table_id > 3) return 0;
                if (hpos + 16 > seg_size) return 0;

                table = (table_class == 0) ? &dc_tables[table_id] : &ac_tables[table_id];
                if (table_class != 0 && table_class != 1) return 0;

                for (int i = 0; i < 16; i++) {
                    table->counts[i] = seg[hpos + i];
                    total += table->counts[i];
                }
                hpos += 16;
                if (total < 0 || total > JPEG_MAX_HUFF_SYMBOLS) return 0;
                if (hpos + total > seg_size) return 0;
                for (int i = 0; i < total; i++) {
                    table->symbols[i] = seg[hpos + i];
                }
                hpos += total;
                build_huff_table(table);
            }
        } else if (marker == 0xDD) {
            if (seg_size != 2) return 0;
            restart_interval = (int)read_be16(seg);
        }

        pos += seg_size;
    }

    if (!scan_ready || comp_count == 0 || width <= 0 || height <= 0) return 0;
    if (out_capacity < width * height * 3) return 0;

    {
        const unsigned char* seg;
        int scan_comp_count;

        if (sos_size < 1) return 0;
        seg = data + sos_pos;
        scan_comp_count = seg[0];
        if (scan_comp_count != comp_count) return 0;
        if (sos_size != 1 + scan_comp_count * 2 + 3) return 0;

        for (int i = 0; i < scan_comp_count; i++) {
            int scan_id = seg[1 + i * 2];
            int huff = seg[1 + i * 2 + 1];
            int found = -1;
            for (int j = 0; j < comp_count; j++) {
                if (comps[j].id == scan_id) {
                    found = j;
                    break;
                }
            }
            if (found < 0) return 0;
            comps[found].td = (huff >> 4) & 0x0F;
            comps[found].ta = huff & 0x0F;
            if (comps[found].td < 0 || comps[found].td > 3 || comps[found].ta < 0 || comps[found].ta > 3) return 0;
            if (!dc_tables[comps[found].td].valid || !ac_tables[comps[found].ta].valid) return 0;
        }

        if (seg[1 + scan_comp_count * 2] != 0) return 0;
        if (seg[1 + scan_comp_count * 2 + 1] != 63) return 0;
        if (seg[1 + scan_comp_count * 2 + 2] != 0) return 0;

        pos = sos_pos + sos_size;
    }

    br.data = data + pos;
    br.size = size - pos;
    br.pos = 0;
    br.bit_buf = 0;
    br.bit_count = 0;

    mcu_cols = (width + max_h * 8 - 1) / (max_h * 8);
    mcu_rows = (height + max_v * 8 - 1) / (max_v * 8);
    restarts_left = restart_interval;
    next_restart = 0;

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int ci = 0; ci < comp_count; ci++) {
                JpegComponent* comp = &comps[ci];
                int blocks = comp->h * comp->v;
                int stride = comp->h * 8;

                for (int i = 0; i < stride * comp->v * 8; i++) {
                    mcu_pixels[ci][i] = 0;
                }

                for (int block_idx = 0; block_idx < blocks; block_idx++) {
                    int block[64];
                    unsigned char spatial[64];
                    int symbol;
                    int dc_bits;
                    int dc_diff;
                    int bx = block_idx % comp->h;
                    int by = block_idx / comp->h;

                    for (int i = 0; i < 64; i++) block[i] = 0;

                    if (!huff_decode(&br, &dc_tables[comp->td], &symbol)) return 0;
                    dc_bits = symbol;
                    if (dc_bits < 0 || dc_bits > 11) return 0;
                    if (!receive_extend(&br, dc_bits, &dc_diff)) return 0;
                    comp->dc_pred += dc_diff;
                    block[0] = comp->dc_pred;

                    {
                        int k = 1;
                        while (k < 64) {
                            int run;
                            int size_bits;
                            int ac_value;

                            if (!huff_decode(&br, &ac_tables[comp->ta], &symbol)) return 0;
                            if (symbol == 0x00) break;
                            if (symbol == 0xF0) {
                                k += 16;
                                continue;
                            }

                            run = (symbol >> 4) & 0x0F;
                            size_bits = symbol & 0x0F;
                            k += run;
                            if (k >= 64) return 0;
                            if (!receive_extend(&br, size_bits, &ac_value)) return 0;
                            block[zigzag_map[k]] = ac_value;
                            k++;
                        }
                    }

                    dequantize_block(block, qt[comp->tq]);
                    idct_block(block, spatial);

                    for (int py = 0; py < 8; py++) {
                        for (int px = 0; px < 8; px++) {
                            int dx = bx * 8 + px;
                            int dy = by * 8 + py;
                            mcu_pixels[ci][dy * stride + dx] = spatial[py * 8 + px];
                        }
                    }
                }
            }

            for (int py = 0; py < max_v * 8; py++) {
                int iy = my * max_v * 8 + py;
                if (iy >= height) continue;
                for (int px = 0; px < max_h * 8; px++) {
                    unsigned char rgb[3];
                    int ix = mx * max_h * 8 + px;
                    if (ix >= width) continue;

                    if (comp_count == 1) {
                        int yv = mcu_pixels[0][py * (comps[0].h * 8) + px];
                        rgb[0] = (unsigned char)yv;
                        rgb[1] = (unsigned char)yv;
                        rgb[2] = (unsigned char)yv;
                    } else if (comp_count == 3) {
                        int values[3];
                        for (int ci = 0; ci < 3; ci++) {
                            int sx = (px * comps[ci].h) / max_h;
                            int sy = (py * comps[ci].v) / max_v;
                            int stride = comps[ci].h * 8;
                            values[ci] = (int)mcu_pixels[ci][sy * stride + sx];
                        }
                        ycbcr_to_rgb(values[0], values[1], values[2], rgb);
                    } else {
                        return 0;
                    }

                    {
                        int out_idx = (iy * width + ix) * 3;
                        out_rgb[out_idx] = rgb[0];
                        out_rgb[out_idx + 1] = rgb[1];
                        out_rgb[out_idx + 2] = rgb[2];
                    }
                }
            }

            if (restart_interval > 0) {
                restarts_left--;
                if (restarts_left == 0) {
                    unsigned char marker1;
                    unsigned char marker2;

                    byte_align(&br);
                    if (br.pos + 1 >= br.size) return 0;

                    do {
                        if (br.pos >= br.size) return 0;
                        marker1 = br.data[br.pos++];
                    } while (marker1 != 0xFF);

                    do {
                        if (br.pos >= br.size) return 0;
                        marker2 = br.data[br.pos++];
                    } while (marker2 == 0xFF);

                    if (marker2 != (unsigned char)(0xD0 + next_restart)) return 0;
                    next_restart = (next_restart + 1) & 7;
                    restarts_left = restart_interval;
                    br.bit_buf = 0;
                    br.bit_count = 0;
                    for (int ci = 0; ci < comp_count; ci++) {
                        comps[ci].dc_pred = 0;
                    }
                }
            }
        }
    }

    out_info->width = width;
    out_info->height = height;
    out_info->components = comp_count;
    out_info->progressive = 0;
    return 1;
}

int jpeg_decode_rgb(const unsigned char* data, int size, unsigned char* out_rgb, int out_capacity, JpegInfo* out_info) {
    JpegInfo probe;

    if (!jpeg_probe(data, size, &probe)) return 0;
    if (probe.progressive) {
        return jpeg_decode_progressive_rgb(data, size, out_rgb, out_capacity, out_info);
    }
    return jpeg_decode_baseline_rgb(data, size, out_rgb, out_capacity, out_info);
}

int jpeg_debug_last_error(void) {
    return jpeg_last_error_code;
}
