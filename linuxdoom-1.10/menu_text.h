#ifndef MENU_TEXT_H
#define MENU_TEXT_H
#include "rb_text.h"
#ifdef __cplusplus
extern "C" {
#endif

/* DOOM-0211: the crisp menu's draw queue, shared by every presenter (Vulkan
   today; SDL under Classic). m_menu.c queues through the rb_text_* / rb_menu_*
   / rb_display_* entry points defined in menu_text.c; a presenter reads the
   lists back and draws them. Positions are display pixels, top-left origin. */

/* One textured quad vertex. The Vulkan vertex input description reads this
   layout field by field, so it must not change. */
typedef struct { float x, y, u, v; unsigned char r, g, b, a; } mt_vertex_t;

/* List capacities in vertices. A frame queuing more drops the tail. */
#define MT_TEXT_CAP   (4096 * 6)   /* up to ~4096 glyphs a frame, 6 verts each */
#define MT_SPRITE_CAP (64 * 6)     /* the skull and logo queue one quad each */

/* Set by m_menu.c on a frame it queued crisp draws; the presenter resets it
   after drawing. */
extern int rb_menu_text_active;

/* The presenter's half. mt_font is the module's font: the presenter bakes into
   it, uploads the pixels, frees them, then calls mt_set_font_ready. mt_reset
   clears everything a presenter set, and runs when that presenter shuts down. */
rb_atlas_font_t* mt_font(void);
void mt_set_font_ready(int ready);
int  mt_font_ready(void);
void mt_set_cursor(int ready, int w, int h);
void mt_set_logo(int ready, int w, int h);
void mt_set_display(int w, int h);
void mt_reset(void);

/* This frame's queued vertices; *n receives the count. */
const mt_vertex_t* mt_text_verts(int* n);
const mt_vertex_t* mt_cursor_verts(int* n);
const mt_vertex_t* mt_logo_verts(int* n);

/* m_menu.c's half (it declares these itself). */
void rb_text_begin(void);
int  rb_text_width(const char* s, float scale);
int  rb_text_line_height(float scale);
void rb_text_draw(const char* s, int x, int y, float scale, unsigned rgba);
int  rb_menu_safe_bottom(void);
int  rb_display_width(void);
int  rb_display_height(void);
void rb_menu_fill(int x, int y, int w, int h, unsigned rgba);
int  rb_menu_cursor_ready(void);
int  rb_menu_cursor_width(int h);
void rb_menu_draw_cursor(int x, int y, int h);
int  rb_menu_logo_ready(void);
int  rb_menu_logo_width(int h);
void rb_menu_draw_logo(int x, int y, int h);
void rb_menu_dim(void);

#ifdef __cplusplus
}
#endif
#endif
