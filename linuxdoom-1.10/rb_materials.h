#ifndef RB_MATERIALS_H
#define RB_MATERIALS_H
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define RB_MAP_COUNT 7
enum { RB_ALB = 0, RB_NRM, RB_RGH, RB_MET, RB_AO, RB_EMIS, RB_HGT };

#define RB_FLAG_POM    1u
#define RB_FLAG_NOPOM  2u
#define RB_FLAG_SPRITE 4u
#define RB_FLAG_BAD    0x80000000u   /* unknown flags token — caller treats row malformed */

typedef struct {
    char          name[9];                     /* DOOM name, NUL-terminated (<=8 chars) */
    int           is_hero;                     /* 1 = hero, 0 = derive */
    char          maps[RB_MAP_COUNT][128];     /* per-map path; "" = no map */
    float         uv_scale;
    unsigned int  flags;
} rb_matrow_t;

/* Trim leading/trailing ASCII whitespace in place; returns the trimmed start. */
static inline char* rb_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static inline unsigned int rb_parse_flags(const char* field) {
    unsigned int f = 0;
    char buf[128];
    strncpy(buf, field, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* start = buf;
    for (char* c = buf; ; c++) {
        if (*c == '|' || *c == '\0') {
            char end = *c; *c = '\0';
            char* tok = rb_trim(start);
            if (*tok) {
                if      (!strcmp(tok, "pom"))    f |= RB_FLAG_POM;
                else if (!strcmp(tok, "noPom"))  f |= RB_FLAG_NOPOM;
                else if (!strcmp(tok, "sprite")) f |= RB_FLAG_SPRITE;
                else                             f |= RB_FLAG_BAD;
            }
            if (end == '\0') break;
            start = c + 1;
        }
    }
    if (f & RB_FLAG_NOPOM) f &= ~RB_FLAG_POM;   /* noPom wins; shader checks one bit */
    return f;
}

/* 1 = data row parsed, 0 = comment/blank (skip), -1 = malformed (log + skip). */
static inline int rb_parse_material_line(const char* line, rb_matrow_t* out) {
    char buf[1024];
    strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* s = rb_trim(buf);
    if (!*s || *s == '#') return 0;             /* comment / blank */

    /* Hand-split into exactly 11 positional columns on ',' (strtok would collapse
       empty cells, which a derive row needs to keep). */
    char* col[11];
    int n = 0;
    col[n++] = s;
    for (char* c = s; *c; c++)
        if (*c == ',') { *c = '\0'; if (n < 11) col[n] = c + 1; n++; }
    if (n != 11) return -1;                     /* wrong column count */

    memset(out, 0, sizeof(*out));
    strncpy(out->name, rb_trim(col[0]), 8); out->name[8] = '\0';

    char* src = rb_trim(col[1]);
    if      (!strcmp(src, "hero"))   out->is_hero = 1;
    else if (!strcmp(src, "derive")) out->is_hero = 0;
    else return -1;                             /* unknown source */

    for (int i = 0; i < RB_MAP_COUNT; i++)
        strncpy(out->maps[i], rb_trim(col[2 + i]), sizeof(out->maps[i]) - 1);

    out->uv_scale = (float)atof(rb_trim(col[9]));
    if (out->uv_scale <= 0.0f) out->uv_scale = 1.0f;   /* blank/invalid -> 1.0 */

    out->flags = rb_parse_flags(rb_trim(col[10]));
    if (out->flags & RB_FLAG_BAD) return -1;    /* unknown flags token -> malformed */
    return 1;
}

#endif
