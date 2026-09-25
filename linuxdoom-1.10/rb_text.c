#include "rb_text.h"
#include <stdlib.h>

/* DOOM-0206: vendored single-header TrueType glyph rasterizer (menu text). Same
   vendored-stb pattern as rb_image.c/stb_image.h (ADR docs/decisions/0002): the
   IMPLEMENTATION macro + #include live in this one small C TU, so the ~5k-line
   library body is compiled exactly once. stb_truetype.h ships far more internal
   static helpers than the bake-only subset we call (BakeFontBitmap/InitFont/
   GetFontOffsetForIndex/ScaleForPixelHeight/GetFontVMetrics), so the unused rest
   trips -Wunused-function under -Wall; scope-silence just this header, not the
   project's. */
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#define RB_FIRST_CHAR 32
#define RB_NUM_CHARS  96   /* printable ASCII 32..127, matches rb_glyph_t glyphs[96] */

/* Atlas square size to try, doubling until stbtt_BakeFontBitmap's "crappy packing"
   fits every glyph (it returns <=0 on partial/no fit). 512 comfortably fits 96
   glyphs at typical menu px_heights; doubling covers unusually large px_height
   without a per-call size argument in the public API. */
#define RB_ATLAS_START 512
#define RB_ATLAS_MAX   4096

/* Big-endian reads for the sfnt table directory. */
static unsigned rb_be16(const unsigned char* p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long rb_be32(const unsigned char* p) {
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* stbtt takes no length and trusts the font's own offsets, so a truncated font is
   read past its end (DOOM-0410). Check that the table directory and every table it
   lists lie inside ttf_len. It bounds the tables, not every offset inside them;
   stbtt is not built for hostile fonts, and the one it bakes is compiled in. */
static int rb_ttf_tables_fit(const unsigned char* ttf, int ttf_len, int offset) {
    if (ttf_len < 12 || offset < 0 || offset > ttf_len - 12) return 0;
    unsigned n = rb_be16(ttf + offset + 4);
    unsigned long dir = (unsigned long)offset + 12, end = dir + 16ul * n;
    if (end > (unsigned long)ttf_len) return 0;
    for (unsigned i = 0; i < n; i++) {
        const unsigned char* rec = ttf + dir + 16ul * i;
        unsigned long toff = rb_be32(rec + 8), tlen = rb_be32(rec + 12);
        if (toff > (unsigned long)ttf_len || tlen > (unsigned long)ttf_len - toff) return 0;
    }
    return 1;
}

int rb_text_bake(const unsigned char* ttf, int ttf_len, int px_height, rb_atlas_font_t* out) {
    if (!ttf || ttf_len < 12 || px_height <= 0 || !out) return 0;   /* < an sfnt header */

    int offset = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (offset < 0) return 0;
    if (!rb_ttf_tables_fit(ttf, ttf_len, offset)) return 0;

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf, offset)) return 0;

    stbtt_bakedchar chardata[RB_NUM_CHARS];
    unsigned char* pixels = NULL;
    int size = 0;
    for (size = RB_ATLAS_START; size <= RB_ATLAS_MAX; size *= 2) {
        pixels = (unsigned char*)malloc((size_t)size * (size_t)size);
        if (!pixels) return 0;                                       /* OOM: never crash */
        int r = stbtt_BakeFontBitmap(ttf, offset, (float)px_height, pixels, size, size,
                                      RB_FIRST_CHAR, RB_NUM_CHARS, chardata);
        if (r > 0) break;                                            /* every glyph fit */
        free(pixels);
        pixels = NULL;
    }
    if (!pixels) return 0;   /* didn't fit even at RB_ATLAS_MAX */

    out->pixels = pixels;
    out->w = size;
    out->h = size;
    out->px_height = px_height;
    for (int i = 0; i < RB_NUM_CHARS; i++) {
        out->glyphs[i].x0 = chardata[i].x0;
        out->glyphs[i].y0 = chardata[i].y0;
        out->glyphs[i].x1 = chardata[i].x1;
        out->glyphs[i].y1 = chardata[i].y1;
        out->glyphs[i].xoff = chardata[i].xoff;
        out->glyphs[i].yoff = chardata[i].yoff;
        out->glyphs[i].xadvance = chardata[i].xadvance;
    }

    float scale = stbtt_ScaleForPixelHeight(&info, (float)px_height);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    out->ascent   = (int)(ascent   * scale);
    out->descent  = (int)(descent  * scale);
    out->line_gap = (int)(line_gap * scale);
    return 1;
}

void rb_text_free_font(rb_atlas_font_t* f) {
    if (f && f->pixels) { free(f->pixels); f->pixels = NULL; }
}

float rb_text_measure(const rb_atlas_font_t* f, const char* s) {
    if (!f || !s) return 0.0f;
    float w = 0.0f;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        int idx = (int)*p - RB_FIRST_CHAR;
        if (idx >= 0 && idx < RB_NUM_CHARS) w += f->glyphs[idx].xadvance;
    }
    return w;
}
