/* DOOM-0211: the crisp menu's draw queue, moved out of r_vulkan.cpp so a
   presenter other than Vulkan can draw it. The logic is DOOM-0206's, unchanged:
   m_menu.c queues glyph, fill, cursor and logo quads here each frame, and the
   live presenter reads the three lists back and draws them over its frame. */

#include <stdio.h>
#include <string.h>

#include "doomstat.h"     /* gamestate */
#include "menu_text.h"

extern int screenblocks;  /* m_menu.c: HUD size 0-10 (DOOM-0148 clamp) */

int rb_menu_text_active = 0;

static rb_atlas_font_t font;
static int fontReady;
static int cursorReady, cursorW, cursorH;
static int logoReady, logoW, logoH;
static int dispW, dispH;

static mt_vertex_t textVerts[MT_TEXT_CAP];
static mt_vertex_t cursorVerts[MT_SPRITE_CAP];
static mt_vertex_t logoVerts[MT_SPRITE_CAP];
static int nText, nCursor, nLogo;

rb_atlas_font_t* mt_font(void)          { return &font; }
void mt_set_font_ready(int ready)       { fontReady = ready; }
int  mt_font_ready(void)                { return fontReady; }
void mt_set_cursor(int ready, int w, int h) { cursorReady = ready; cursorW = w; cursorH = h; }
void mt_set_logo(int ready, int w, int h)   { logoReady = ready; logoW = w; logoH = h; }
void mt_set_display(int w, int h)       { dispW = w; dispH = h; }

void mt_reset(void)
{
    rb_text_free_font(&font);   /* no-op if the pixels were already freed after upload */
    memset(&font, 0, sizeof(font));
    fontReady = 0;
    cursorReady = cursorW = cursorH = 0;
    logoReady = logoW = logoH = 0;
    dispW = dispH = 0;
    nText = nCursor = nLogo = 0;
}

const mt_vertex_t* mt_text_verts(int* n)   { *n = nText;   return textVerts; }
const mt_vertex_t* mt_cursor_verts(int* n) { *n = nCursor; return cursorVerts; }
const mt_vertex_t* mt_logo_verts(int* n)   { *n = nLogo;   return logoVerts; }

/* Append one quad (two triangles: a0 a1 a2, a0 a2 a3) to a list; a full list
   drops it, as the presenter's capacity clip dropped the tail before. */
static void PushQuad(mt_vertex_t* list, int* n, int cap,
                     float x0, float y0, float x1, float y1,
                     float u0, float v0, float u1, float v1,
                     unsigned char cr, unsigned char cg, unsigned char cb, unsigned char ca)
{
    mt_vertex_t a0 = { x0, y0, u0, v0, cr, cg, cb, ca };
    mt_vertex_t a1 = { x1, y0, u1, v0, cr, cg, cb, ca };
    mt_vertex_t a2 = { x1, y1, u1, v1, cr, cg, cb, ca };
    mt_vertex_t a3 = { x0, y1, u0, v1, cr, cg, cb, ca };
    if (*n + 6 > cap) return;
    list[(*n)++] = a0; list[(*n)++] = a1; list[(*n)++] = a2;
    list[(*n)++] = a0; list[(*n)++] = a2; list[(*n)++] = a3;
}

void rb_text_begin(void)
{
    nText = nCursor = nLogo = 0;
}

int rb_text_width(const char* s, float scale)
{
    if (!fontReady) return 0;
    return (int)(rb_text_measure(&font, s) * scale + 0.5f);
}

int rb_text_line_height(float scale)
{
    if (!fontReady) return 0;
    return (int)((float)font.px_height * scale + 0.5f);
}

/* Emit one string's glyph quads at (x,y) top-left with an explicit RGBA. Shared by the shadow
   pass and the main pass of rb_text_draw. */
static void EmitTextQuads(const char* s, float x, float y, float scale,
                          unsigned char cr, unsigned char cg, unsigned char cb, unsigned char ca)
{
    const float aw = (float)font.w, ah = (float)font.h;
    float penX = x;
    /* The API's y is the text's top-left; glyph xoff/yoff are baseline-relative, so drop the
       pen to the baseline (top + ascent). ascent was baked in pixels at px_height. */
    const float baseY = y + (float)font.ascent * scale;
    const unsigned char* p;
    for (p = (const unsigned char*)s; *p; p++)
    {
        int idx = (int)*p - 32;
        const rb_glyph_t* gl;
        float x0, y0;
        if (idx < 0 || idx >= 96) continue;   /* non-printable / out of the baked ASCII range */
        gl = &font.glyphs[idx];
        x0 = penX + gl->xoff * scale;
        y0 = baseY + gl->yoff * scale;
        PushQuad(textVerts, &nText, MT_TEXT_CAP,
                 x0, y0, x0 + (float)(gl->x1 - gl->x0) * scale, y0 + (float)(gl->y1 - gl->y0) * scale,
                 (float)gl->x0 / aw, (float)gl->y0 / ah, (float)gl->x1 / aw, (float)gl->y1 / ah,
                 cr, cg, cb, ca);
        penX += gl->xadvance * scale;
    }
}

void rb_text_draw(const char* s, int x, int y, float scale, unsigned rgba)
{
    unsigned char cr, cg, cb, ca;
    float shOff;
    if (!fontReady || !s) return;
    cr = (unsigned char)((rgba >> 24) & 0xFF);
    cg = (unsigned char)((rgba >> 16) & 0xFF);
    cb = (unsigned char)((rgba >>  8) & 0xFF);
    ca = (unsigned char)( rgba        & 0xFF);
    /* DOOM-0206 (L5): a soft drop-shadow for legibility over the dimmed view. Draw the same
       string in near-black one glyph-fraction down-right first, then the real colour on top.
       Offset scales with the font so it reads the same at any resolution (clamped 1..3px). */
    shOff = (float)font.ascent * scale / 18.0f;
    if (shOff < 1.0f) shOff = 1.0f;
    if (shOff > 3.0f) shOff = 3.0f;
    EmitTextQuads(s, (float)x + shOff, (float)y + shOff, scale, 0, 0, 0, (unsigned char)(ca * 3 / 4));
    EmitTextQuads(s, (float)x, (float)y, scale, cr, cg, cb, ca);
}

/* DOOM-0206 (L2): INV-2, the HUD-safe bound. Returns the display-pixel Y below which nothing
   may draw -- the status bar's top edge while it's on screen, else the full display height.

   screenblocks < 11 is DOOM-0148's always-true-in-game invariant (M_Init clamps screenblocks
   to <= 10, so 11's fullscreen-no-HUD view is currently unreachable) -- checked anyway so this
   stays correct if that clamp is ever lifted. 200/32 are ORIGHEIGHT/ST_HEIGHT. */
int rb_menu_safe_bottom(void)
{
    static int logged = 0;
    int safeBottom = dispH;
    if (gamestate == GS_LEVEL && screenblocks < 11)
        safeBottom = dispH * (200 - 32) / 200;   /* 200=ORIGHEIGHT, 32=ST_HEIGHT */
    if (!logged)
    {
        printf("RB_Vulkan: rb_menu_safe_bottom = %d (dispH=%d, gamestate=%d, screenblocks=%d)\n",
               safeBottom, dispH, (int)gamestate, screenblocks);
        fflush(stdout);
        logged = 1;
    }
    return safeBottom;
}

/* DOOM-0206 (L3): the display extent, in display pixels. The crisp menus centre their title,
   right-align values and map the skull's virtual-Y from these. */
int rb_display_width(void)  { return dispW; }
int rb_display_height(void) { return dispH; }

/* DOOM-0206 (L3): a solid-colour quad in display pixels -- the one quad path shared by the menu
   dim and the crisp Brightness slider. Colour via the reserved full-coverage atlas texel (0,0),
   so it needs no extra pipeline. rgba is 0xRRGGBBAA. Queued into the text list. */
void rb_menu_fill(int x, int y, int w, int h, unsigned rgba)
{
    float u, v;
    if (!fontReady) return;
    u = 0.5f / (float)font.w;           /* texel (0,0) centre (full coverage) */
    v = 0.5f / (float)font.h;
    PushQuad(textVerts, &nText, MT_TEXT_CAP,
             (float)x, (float)y, (float)(x+w), (float)(y+h), u, v, u, v,
             (unsigned char)((rgba >> 24) & 0xFF), (unsigned char)((rgba >> 16) & 0xFF),
             (unsigned char)((rgba >>  8) & 0xFF), (unsigned char)( rgba        & 0xFF));
}

/* DOOM-0206 v2: the crisp menu cursor -- the real WAD skull M_SKULL1, decoded to RGBA and
   brightened by the presenter, sized to a text row. Its own list, because it draws with an RGBA
   texture rather than the glyph atlas. Ready only if the presenter made the texture; m_menu
   falls back to the paletted skull otherwise. */
int rb_menu_cursor_ready(void)
{
    return cursorReady;
}

/* Drawn width (px) of the cursor at target height h, keeping the sprite's aspect -- m_menu uses
   it to place the cursor fully left of the label column. */
int rb_menu_cursor_width(int h)
{
    if (!cursorReady || h <= 0 || cursorH <= 0) return 0;
    return (int)((float)h * (float)cursorW / (float)cursorH + 0.5f);
}

/* Draw the skull cursor with its top-left at (x,y), target height h (px). One RGBA quad over
   the whole texture; the brightness is baked into it, so the tint is plain white. */
void rb_menu_draw_cursor(int x, int y, int h)
{
    float x0, y0;
    if (!cursorReady || h <= 0 || cursorH <= 0) return;
    x0 = (float)x; y0 = (float)y;
    PushQuad(cursorVerts, &nCursor, MT_SPRITE_CAP,
             x0, y0, x0 + (float)h * (float)cursorW / (float)cursorH, y0 + (float)h,
             0.f, 0.f, 1.f, 1.f, 255, 255, 255, 255);
}

/* DOOM-0206: the M_DOOM logo sprite (main-menu crisp title). Mirrors the cursor API. */
int rb_menu_logo_ready(void)
{
    return logoReady;
}

/* Drawn width (px) of the logo at target height h, keeping the lump's aspect. */
int rb_menu_logo_width(int h)
{
    if (!logoReady || h <= 0 || logoH <= 0) return 0;
    return (int)((float)h * (float)logoW / (float)logoH + 0.5f);
}

/* Draw the M_DOOM logo with its top-left at (x,y), target height h (px). One RGBA quad; the
   logo carries its own colours, so the tint is plain white and it draws bright over the dim. */
void rb_menu_draw_logo(int x, int y, int h)
{
    float x0, y0;
    if (!logoReady || h <= 0 || logoH <= 0) return;
    x0 = (float)x; y0 = (float)y;
    PushQuad(logoVerts, &nLogo, MT_SPRITE_CAP,
             x0, y0, x0 + (float)h * (float)logoW / (float)logoH, y0 + (float)h,
             0.f, 0.f, 1.f, 1.f, 255, 255, 255, 255);
}

/* DOOM-0206 (L1b/L2): the play-view dim quad (menu backdrop). Darkens the world behind the
   menu but leaves the status bar undimmed (rb_menu_safe_bottom, INV-2) so the HUD stays
   readable. The tier gate lives in the caller (m_menu). */
void rb_menu_dim(void)
{
    if (!fontReady) return;
    /* 0x000000A0 == ~63% black over the play view, from y=0 to the status-bar top (INV-2). */
    rb_menu_fill(0, 0, dispW, rb_menu_safe_bottom(), 0x000000A0u);
}
