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
    /* DOOM-0206 v2: an optional menu-cursor sprite (the skull) baked into an extra
       strip below the glyphs. The source PNG packs THREE coverage masks in its RGB
       channels (bone body / dark recesses / red eye glow); each becomes an R8 region
       here, and the draw path stacks them in DOOM colours through the single-channel
       tinted-quad pipeline. cur_layers==0 means no cursor was baked. */
    unsigned short cur_x0[3], cur_y0[3], cur_x1[3], cur_y1[3];   /* per-layer atlas rects, px */
    int cur_layers;
} rb_atlas_font_t;

/* Bakes 'ttf' (raw .ttf/.otf bytes, ttf_len long) at px_height into out. 1 ok / 0 fail
   (bad font data, zero/negative px_height, or OOM -- never crashes). On success,
   out->pixels is heap-allocated; free it with rb_text_free_font. */
int  rb_text_bake(const unsigned char* ttf, int ttf_len, int px_height, rb_atlas_font_t* out);
void rb_text_free_font(rb_atlas_font_t* f);

/* DOOM-0206 v2: bake a menu-cursor sprite into a new strip below the glyph atlas from a
   decoded RGBA buffer (sw*sh*4 bytes). The R/G/B channels are stored as three side-by-side
   R8 coverage regions (recorded in f->cur_* / f->cur_layers) that the draw path stacks in
   colour. Reallocates f->pixels (grows f->h); glyph rects stay valid (absolute coords, same
   sample position). 1 ok / 0 fail (bad args, 3 masks too wide for the atlas, or OOM -- f left
   unchanged). Caller decodes the PNG (keeps this TU free of the image-codec dependency) and
   calls this after rb_text_bake, before uploading f->pixels to the GPU. */
int  rb_text_add_cursor(rb_atlas_font_t* f, const unsigned char* rgba, int sw, int sh);

/* Pixel width of 's' at f's baked size (sum of each character's xadvance). */
float rb_text_measure(const rb_atlas_font_t* f, const char* s);

#ifdef __cplusplus
}
#endif
#endif
