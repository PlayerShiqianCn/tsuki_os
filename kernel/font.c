#include "font.h"
#include "fs.h"
#include "heap.h"
#include "utils.h"
#include "klog.h"

#define FONT_FIRST_CODEPOINT 32
#define FONT_LAST_CODEPOINT 126
#define FONT_CACHE_COUNT (FONT_LAST_CODEPOINT - FONT_FIRST_CODEPOINT + 1)
#define FONT_BITMAP_W 18
#define FONT_BITMAP_H FONT_LINE_HEIGHT
#define FONT_SUPERSAMPLE 4
#define FONT_MAX_FILE_SIZE 450000u
#define FONT_MAX_POINTS 256
#define FONT_MAX_CONTOURS 32
#define FONT_MAX_EDGES 512

typedef struct {
    unsigned char valid;
    signed char x_offset;
    unsigned char width;
    unsigned char advance;
    unsigned char alpha[FONT_BITMAP_W * FONT_BITMAP_H];
} CachedGlyph;

typedef struct {
    int x0, y0, x1, y1;
} FontEdge;

static unsigned char* font_data;
static unsigned int font_size;
static unsigned int table_head, table_hhea, table_maxp, table_hmtx;
static unsigned int table_loca, table_glyf, table_cmap;
static unsigned int cmap4;
static unsigned short units_per_em;
static unsigned short num_glyphs;
static unsigned short num_hmetrics;
static short ascender;
static short descender;
static short loca_format;
static int line_units;
static int ttf_ready;
static CachedGlyph glyph_cache[FONT_CACHE_COUNT];

static int work_x[FONT_MAX_POINTS];
static int work_y[FONT_MAX_POINTS];
static unsigned char work_flags[FONT_MAX_POINTS];
static unsigned short work_end_pts[FONT_MAX_CONTOURS];
static FontEdge work_edges[FONT_MAX_EDGES];
static int work_edge_count;

static int range_ok(unsigned int off, unsigned int len) {
    return off <= font_size && len <= font_size - off;
}

static unsigned short be16(unsigned int off) {
    if (!range_ok(off, 2)) return 0;
    return (unsigned short)(((unsigned short)font_data[off] << 8) | font_data[off + 1]);
}

static short bes16(unsigned int off) {
    return (short)be16(off);
}

static unsigned int be32(unsigned int off) {
    if (!range_ok(off, 4)) return 0;
    return ((unsigned int)font_data[off] << 24) |
           ((unsigned int)font_data[off + 1] << 16) |
           ((unsigned int)font_data[off + 2] << 8) |
           (unsigned int)font_data[off + 3];
}

static int tag_eq(unsigned int off, const char* tag) {
    if (!range_ok(off, 4)) return 0;
    return font_data[off] == (unsigned char)tag[0] &&
           font_data[off + 1] == (unsigned char)tag[1] &&
           font_data[off + 2] == (unsigned char)tag[2] &&
           font_data[off + 3] == (unsigned char)tag[3];
}

static unsigned int find_table(const char* tag) {
    unsigned short count;
    unsigned int dir;
    if (!range_ok(0, 12)) return 0;
    count = be16(4);
    dir = 12;
    if (!range_ok(dir, (unsigned int)count * 16u)) return 0;
    for (unsigned int i = 0; i < count; i++) {
        unsigned int rec = dir + i * 16u;
        if (tag_eq(rec, tag)) {
            unsigned int off = be32(rec + 8);
            unsigned int len = be32(rec + 12);
            if (!range_ok(off, len)) return 0;
            return off;
        }
    }
    return 0;
}

static int find_cmap4(void) {
    unsigned short count;
    if (!table_cmap || !range_ok(table_cmap, 4)) return 0;
    count = be16(table_cmap + 2);
    if (!range_ok(table_cmap + 4, (unsigned int)count * 8u)) return 0;

    for (int pass = 0; pass < 2; pass++) {
        for (unsigned int i = 0; i < count; i++) {
            unsigned int rec = table_cmap + 4 + i * 8u;
            unsigned short platform = be16(rec);
            unsigned short encoding = be16(rec + 2);
            unsigned int rel = be32(rec + 4);
            unsigned int sub = table_cmap + rel;
            if (!range_ok(sub, 2) || be16(sub) != 4) continue;
            if (pass == 0 && !(platform == 3 && (encoding == 1 || encoding == 0))) continue;
            cmap4 = sub;
            return 1;
        }
    }
    return 0;
}

static unsigned short cmap4_glyph(unsigned int codepoint) {
    unsigned short seg_count;
    unsigned int end_codes;
    unsigned int start_codes;
    unsigned int deltas;
    unsigned int range_offsets;

    if (!cmap4 || codepoint > 0xFFFFu || !range_ok(cmap4, 16)) return 0;
    seg_count = (unsigned short)(be16(cmap4 + 6) / 2);
    end_codes = cmap4 + 14;
    start_codes = end_codes + (unsigned int)seg_count * 2u + 2u;
    deltas = start_codes + (unsigned int)seg_count * 2u;
    range_offsets = deltas + (unsigned int)seg_count * 2u;
    if (!range_ok(range_offsets, (unsigned int)seg_count * 2u)) return 0;

    for (unsigned int i = 0; i < seg_count; i++) {
        unsigned short end = be16(end_codes + i * 2u);
        unsigned short start = be16(start_codes + i * 2u);
        if (codepoint < start || codepoint > end) continue;
        {
            short delta = bes16(deltas + i * 2u);
            unsigned short ro = be16(range_offsets + i * 2u);
            if (ro == 0) return (unsigned short)((codepoint + delta) & 0xFFFFu);
            {
                unsigned int ro_addr = range_offsets + i * 2u;
                unsigned int glyph_addr = ro_addr + ro + (codepoint - start) * 2u;
                unsigned short glyph;
                if (!range_ok(glyph_addr, 2)) return 0;
                glyph = be16(glyph_addr);
                if (glyph != 0) glyph = (unsigned short)((glyph + delta) & 0xFFFFu);
                return glyph;
            }
        }
    }
    return 0;
}

static unsigned int glyph_offset(unsigned short glyph) {
    unsigned int off;
    if (glyph >= num_glyphs || !table_loca) return 0;
    if (loca_format == 0) {
        unsigned int p = table_loca + (unsigned int)glyph * 2u;
        if (!range_ok(p, 2)) return 0;
        off = (unsigned int)be16(p) * 2u;
    } else {
        unsigned int p = table_loca + (unsigned int)glyph * 4u;
        if (!range_ok(p, 4)) return 0;
        off = be32(p);
    }
    return off;
}

static unsigned int glyph_end_offset(unsigned short glyph) {
    if ((unsigned int)glyph + 1u > num_glyphs || !table_loca) return 0;
    if (loca_format == 0) {
        unsigned int p = table_loca + ((unsigned int)glyph + 1u) * 2u;
        if (!range_ok(p, 2)) return 0;
        return (unsigned int)be16(p) * 2u;
    }
    {
        unsigned int p = table_loca + ((unsigned int)glyph + 1u) * 4u;
        if (!range_ok(p, 4)) return 0;
        return be32(p);
    }
}

static int glyph_advance(unsigned short glyph) {
    unsigned int metric;
    unsigned short advance;
    if (!table_hmtx || num_hmetrics == 0) return 8;
    if (glyph < num_hmetrics) metric = table_hmtx + (unsigned int)glyph * 4u;
    else metric = table_hmtx + (unsigned int)(num_hmetrics - 1u) * 4u;
    if (!range_ok(metric, 2)) return 8;
    advance = be16(metric);
    if (line_units <= 0) return 8;
    {
        int px = ((int)advance * FONT_LINE_HEIGHT + line_units / 2) / line_units;
        if (px < 2) px = 2;
        if (px > 12) px = 12;
        return px;
    }
}

static int scale_font_x(int x, int origin) {
    if (line_units <= 0) return 0;
    return ((x - origin) * FONT_LINE_HEIGHT * 256) / line_units;
}

static int scale_font_y(int y) {
    int baseline;
    if (line_units <= 0) return 0;
    baseline = ((int)ascender * FONT_LINE_HEIGHT * 256) / line_units;
    return baseline - (y * FONT_LINE_HEIGHT * 256) / line_units;
}

static void add_edge(int x0, int y0, int x1, int y1) {
    if (work_edge_count >= FONT_MAX_EDGES) return;
    if (x0 == x1 && y0 == y1) return;
    work_edges[work_edge_count].x0 = x0;
    work_edges[work_edge_count].y0 = y0;
    work_edges[work_edge_count].x1 = x1;
    work_edges[work_edge_count].y1 = y1;
    work_edge_count++;
}

static void add_quad(int x0, int y0, int cx, int cy, int x1, int y1) {
    int px = x0;
    int py = y0;
    for (int step = 1; step <= 4; step++) {
        int u = 4 - step;
        int qx = (u * u * x0 + 2 * u * step * cx + step * step * x1) / 16;
        int qy = (u * u * y0 + 2 * u * step * cy + step * step * y1) / 16;
        add_edge(px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

static int sample_inside(int x, int y) {
    int inside = 0;
    for (int i = 0; i < work_edge_count; i++) {
        int x0 = work_edges[i].x0;
        int y0 = work_edges[i].y0;
        int x1 = work_edges[i].x1;
        int y1 = work_edges[i].y1;
        if ((y0 > y) == (y1 > y)) continue;
        {
            int dy = y1 - y0;
            int lhs = (y - y0) * (x1 - x0);
            int rhs = (x - x0) * dy;
            if ((dy > 0 && lhs > rhs) || (dy < 0 && lhs < rhs)) inside = !inside;
        }
    }
    return inside;
}

static void raster_edges(CachedGlyph* out) {
    for (int y = 0; y < FONT_BITMAP_H; y++) {
        for (int x = 0; x < FONT_BITMAP_W; x++) {
            int covered = 0;
            for (int sy = 0; sy < FONT_SUPERSAMPLE; sy++) {
                int py = (y << 8) + ((2 * sy + 1) * 256) / (2 * FONT_SUPERSAMPLE);
                for (int sx = 0; sx < FONT_SUPERSAMPLE; sx++) {
                    int px = (x << 8) + ((2 * sx + 1) * 256) / (2 * FONT_SUPERSAMPLE);
                    if (sample_inside(px, py)) covered++;
                }
            }
            /* Tiny UI text is rendered on a 320x200 logical canvas and then
             * integer-scaled to the physical framebuffer. Full 16-level
             * coverage looks muddy at this size, so quantize coverage to a
             * crisp three-level mask while retaining a small edge hint. */
            if (covered >= 10) out->alpha[y * FONT_BITMAP_W + x] = 255;
            else if (covered >= 5) out->alpha[y * FONT_BITMAP_W + x] = 176;
            else out->alpha[y * FONT_BITMAP_W + x] = 0;
        }
    }
}

static int build_edges_for_glyph(unsigned short glyph, CachedGlyph* out) {
    unsigned int rel = glyph_offset(glyph);
    unsigned int end_rel = glyph_end_offset(glyph);
    unsigned int g;
    short contours;
    short x_min, x_max;
    unsigned int p;
    int point_count;
    int x, y;

    if (end_rel < rel) return 0;
    if (end_rel == rel) {
        out->valid = 1;
        out->width = 0;
        return 1;
    }
    g = table_glyf + rel;
    if (!range_ok(g, end_rel - rel) || !range_ok(g, 10)) return 0;
    contours = bes16(g);
    x_min = bes16(g + 2);
    x_max = bes16(g + 6);
    if (contours < 0 || contours > FONT_MAX_CONTOURS) return 0;
    if (contours == 0) {
        out->valid = 1;
        out->width = 0;
        return 1;
    }

    p = g + 10;
    if (!range_ok(p, (unsigned int)contours * 2u)) return 0;
    for (int i = 0; i < contours; i++) work_end_pts[i] = be16(p + (unsigned int)i * 2u);
    point_count = (int)work_end_pts[contours - 1] + 1;
    if (point_count <= 0 || point_count > FONT_MAX_POINTS) return 0;
    p += (unsigned int)contours * 2u;
    if (!range_ok(p, 2)) return 0;
    {
        unsigned short instruction_len = be16(p);
        p += 2;
        if (!range_ok(p, instruction_len)) return 0;
        p += instruction_len;
    }

    for (int i = 0; i < point_count;) {
        unsigned char f;
        if (!range_ok(p, 1)) return 0;
        f = font_data[p++];
        work_flags[i++] = f;
        if (f & 0x08) {
            unsigned char repeat;
            if (!range_ok(p, 1)) return 0;
            repeat = font_data[p++];
            if (i + repeat > point_count) return 0;
            while (repeat--) work_flags[i++] = f;
        }
    }

    x = 0;
    for (int i = 0; i < point_count; i++) {
        unsigned char f = work_flags[i];
        if (f & 0x02) {
            unsigned char d;
            if (!range_ok(p, 1)) return 0;
            d = font_data[p++];
            x += (f & 0x10) ? (int)d : -(int)d;
        } else if (!(f & 0x10)) {
            if (!range_ok(p, 2)) return 0;
            x += bes16(p);
            p += 2;
        }
        work_x[i] = x;
    }

    y = 0;
    for (int i = 0; i < point_count; i++) {
        unsigned char f = work_flags[i];
        if (f & 0x04) {
            unsigned char d;
            if (!range_ok(p, 1)) return 0;
            d = font_data[p++];
            y += (f & 0x20) ? (int)d : -(int)d;
        } else if (!(f & 0x20)) {
            if (!range_ok(p, 2)) return 0;
            y += bes16(p);
            p += 2;
        }
        work_y[i] = y;
    }

    {
        int scaled_min = (x_min * FONT_LINE_HEIGHT) / line_units;
        int scaled_width = ((x_max - x_min) * FONT_LINE_HEIGHT + line_units - 1) / line_units;
        if (scaled_min < -8) scaled_min = -8;
        if (scaled_min > 8) scaled_min = 8;
        if (scaled_width < 1) scaled_width = 1;
        if (scaled_width > FONT_BITMAP_W) scaled_width = FONT_BITMAP_W;
        out->x_offset = (signed char)scaled_min;
        out->width = (unsigned char)scaled_width;
    }

    work_edge_count = 0;
    {
        int contour_start = 0;
        for (int c = 0; c < contours; c++) {
            int contour_end = work_end_pts[c];
            int sx, sy, cx, cy;
            int i;
            if (contour_end < contour_start || contour_end >= point_count) return 0;

            if (work_flags[contour_start] & 1) {
                sx = work_x[contour_start];
                sy = work_y[contour_start];
                i = contour_start + 1;
            } else if (work_flags[contour_end] & 1) {
                sx = work_x[contour_end];
                sy = work_y[contour_end];
                i = contour_start;
            } else {
                sx = (work_x[contour_start] + work_x[contour_end]) / 2;
                sy = (work_y[contour_start] + work_y[contour_end]) / 2;
                i = contour_start;
            }

            cx = sx;
            cy = sy;
            while (i <= contour_end) {
                int px = work_x[i];
                int py = work_y[i];
                if (work_flags[i] & 1) {
                    add_edge(scale_font_x(cx, x_min), scale_font_y(cy),
                             scale_font_x(px, x_min), scale_font_y(py));
                    cx = px;
                    cy = py;
                    i++;
                } else {
                    int nx, ny;
                    if (i == contour_end) {
                        nx = sx;
                        ny = sy;
                        add_quad(scale_font_x(cx, x_min), scale_font_y(cy),
                                 scale_font_x(px, x_min), scale_font_y(py),
                                 scale_font_x(nx, x_min), scale_font_y(ny));
                        cx = nx;
                        cy = ny;
                        i++;
                    } else if (work_flags[i + 1] & 1) {
                        nx = work_x[i + 1];
                        ny = work_y[i + 1];
                        add_quad(scale_font_x(cx, x_min), scale_font_y(cy),
                                 scale_font_x(px, x_min), scale_font_y(py),
                                 scale_font_x(nx, x_min), scale_font_y(ny));
                        cx = nx;
                        cy = ny;
                        i += 2;
                    } else {
                        nx = (px + work_x[i + 1]) / 2;
                        ny = (py + work_y[i + 1]) / 2;
                        add_quad(scale_font_x(cx, x_min), scale_font_y(cy),
                                 scale_font_x(px, x_min), scale_font_y(py),
                                 scale_font_x(nx, x_min), scale_font_y(ny));
                        cx = nx;
                        cy = ny;
                        i++;
                    }
                }
            }
            if (cx != sx || cy != sy) {
                add_edge(scale_font_x(cx, x_min), scale_font_y(cy),
                         scale_font_x(sx, x_min), scale_font_y(sy));
            }
            contour_start = contour_end + 1;
        }
    }

    if (work_edge_count <= 0) return 0;
    raster_edges(out);
    out->valid = 1;
    return 1;
}

static int parse_font(void) {
    table_head = find_table("head");
    table_hhea = find_table("hhea");
    table_maxp = find_table("maxp");
    table_hmtx = find_table("hmtx");
    table_loca = find_table("loca");
    table_glyf = find_table("glyf");
    table_cmap = find_table("cmap");
    if (!table_head || !table_hhea || !table_maxp || !table_hmtx ||
        !table_loca || !table_glyf || !table_cmap) return 0;
    if (!range_ok(table_head, 54) || !range_ok(table_hhea, 36) || !range_ok(table_maxp, 6)) return 0;

    units_per_em = be16(table_head + 18);
    loca_format = bes16(table_head + 50);
    ascender = bes16(table_hhea + 4);
    descender = bes16(table_hhea + 6);
    num_hmetrics = be16(table_hhea + 34);
    num_glyphs = be16(table_maxp + 4);
    line_units = (int)ascender - (int)descender;
    if (units_per_em == 0 || line_units <= 0 || num_glyphs == 0 || num_hmetrics == 0) return 0;
    if (loca_format != 0 && loca_format != 1) return 0;
    if (!find_cmap4()) return 0;
    return 1;
}

void font_init(void) {
    SystemFile file;
    int read;
    ttf_ready = 0;
    font_data = 0;
    font_size = 0;
    memset(glyph_cache, 0, sizeof(glyph_cache));

    if (!sys_file_open("system/ui.ttf", &file)) {
        klog_write("font fallback: no ui.ttf");
        return;
    }
    if (file.size == 0 || file.size > FONT_MAX_FILE_SIZE) {
        klog_write("font fallback: bad size");
        return;
    }

    font_data = (unsigned char*)malloc(file.size);
    if (!font_data) {
        klog_write("font fallback: oom");
        return;
    }
    font_size = file.size;
    read = fs_read_file("system/ui.ttf", font_data, font_size);
    if (read != (int)font_size || !parse_font()) {
        free(font_data);
        font_data = 0;
        font_size = 0;
        klog_write("font fallback: parse");
        return;
    }

    for (unsigned int cp = FONT_FIRST_CODEPOINT; cp <= FONT_LAST_CODEPOINT; cp++) {
        CachedGlyph* out = &glyph_cache[cp - FONT_FIRST_CODEPOINT];
        unsigned short glyph = cmap4_glyph(cp);
        out->advance = (unsigned char)glyph_advance(glyph);
        if (cp == ' ') {
            out->valid = 1;
            out->width = 0;
            continue;
        }
        build_edges_for_glyph(glyph, out);
    }

    /* Glyphs are fully rasterized into the static cache now. Release the
     * source font so a larger, better hinted face does not permanently consume
     * most of the tiny kernel heap. */
    free(font_data);
    font_data = 0;
    font_size = 0;
    ttf_ready = 1;
    klog_write("font init ttf");
}

int font_is_ttf_ready(void) {
    return ttf_ready;
}

int font_get_glyph(unsigned int codepoint, FontGlyph* out) {
    CachedGlyph* cached;
    if (!out || !ttf_ready || codepoint < FONT_FIRST_CODEPOINT || codepoint > FONT_LAST_CODEPOINT) return 0;
    cached = &glyph_cache[codepoint - FONT_FIRST_CODEPOINT];
    if (!cached->valid) return 0;
    out->width = cached->width;
    out->height = FONT_BITMAP_H;
    out->x_offset = cached->x_offset;
    out->advance = cached->advance ? cached->advance : 2;
    out->stride = FONT_BITMAP_W;
    out->alpha = cached->alpha;
    return 1;
}

int font_text_width(const char* text) {
    int width = 0;
    int line_width = 0;
    if (!text) return 0;
    for (int i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') {
            if (line_width > width) width = line_width;
            line_width = 0;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') {
            line_width += ttf_ready ? 8 : 16;
            continue;
        }
        if (ttf_ready && c >= FONT_FIRST_CODEPOINT && c <= FONT_LAST_CODEPOINT) {
            CachedGlyph* cached = &glyph_cache[c - FONT_FIRST_CODEPOINT];
            line_width += cached->valid ? (cached->advance ? cached->advance : 2) : 8;
        } else {
            line_width += 8;
        }
    }
    if (line_width > width) width = line_width;
    return width;
}
