// jpeg.c — JPEG 解码器入口：探测、基线（SOF0）解码、渐进式分发。
// 解码原语在 jpeg_core.c，渐进式路径在 jpeg_progressive.c，
// 共享类型与跨文件原型见 jpeg_internal.h，公开 API 见 jpeg.h。

#include "jpeg_internal.h"

int jpeg_last_error_code = 0;

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
                if (!huff_symbols_valid(table_class, table->symbols, total)) return 0;
                hpos += total;
                if (!build_huff_table(table)) return 0;
            }
        } else if (marker == 0xDD) {
            if (seg_size != 2) return 0;
            restart_interval = (int)read_be16(seg);
        }

        pos += seg_size;
    }

    if (!scan_ready || comp_count == 0 || width <= 0 || height <= 0) return 0;
    {
        int required;
        if (!required_rgb_capacity(width, height, &required) || out_capacity < required) return 0;
    }

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
                    if (!add_dc_diff(comp, dc_diff)) return 0;
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
                            if (size_bits == 0 || size_bits > 10) return 0;
                            k += run;
                            if (k >= 64) return 0;
                            if (!receive_extend(&br, size_bits, &ac_value)) return 0;
                            block[zigzag_map[k]] = ac_value;
                            k++;
                        }
                    }

                    dequantize_block(block, qt[comp->tq]);
                    if (!idct_block(block, spatial)) return 0;

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
