#ifndef FONT_H
#define FONT_H

#include "theme.h"

#define FONT_LINE_HEIGHT TSUKI_FONT_HEIGHT

typedef struct {
    int width;
    int height;
    int x_offset;
    int advance;
    int stride;
    const unsigned char* alpha;
} FontGlyph;

void font_init(void);
int font_is_ttf_ready(void);
int font_get_glyph(unsigned int codepoint, FontGlyph* out);
int font_text_width(const char* text);

#endif
