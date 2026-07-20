#ifndef RB_TEXT_H
#define RB_TEXT_H
#ifdef __cplusplus
extern "C" {
#endif

/* Atlas uv rect (pixels) + placement for one baked glyph. Field-for-field the
   same shape as stb_truetype's stbtt_bakedchar, kept as our own type so callers
   (and the later GPU text pipeline) never need to see stb_truetype.h. */
typedef struct { unsigned short x0,y0,x1,y1; float xoff,yoff,xadvance; } rb_glyph_t;

/* R8 (single-channel) atlas bitmap + metrics for printable ASCII 32..127 (96 glyphs). */
typedef struct {
    unsigned char* pixels;   /* w*h bytes, R8; caller frees via rb_text_free_font */
    int w, h;
    rb_glyph_t glyphs[96];
    int px_height;
    int ascent, descent, line_gap;
} rb_atlas_font_t;

/* Bakes 'ttf' (raw .ttf/.otf bytes, ttf_len long) at px_height into out. 1 ok / 0 fail
   (bad font data, zero/negative px_height, or OOM -- never crashes). On success,
   out->pixels is heap-allocated; free it with rb_text_free_font. */
int  rb_text_bake(const unsigned char* ttf, int ttf_len, int px_height, rb_atlas_font_t* out);
void rb_text_free_font(rb_atlas_font_t* f);

/* Pixel width of 's' at f's baked size (sum of each character's xadvance). */
float rb_text_measure(const rb_atlas_font_t* f, const char* s);

#ifdef __cplusplus
}
#endif
#endif
