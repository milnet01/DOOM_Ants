#ifndef RB_MATERIALS_H
#define RB_MATERIALS_H
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>

#define RB_MAP_COUNT 7
enum { RB_ALB = 0, RB_NRM, RB_RGH, RB_MET, RB_AO, RB_EMIS, RB_HGT };

#define RB_FLAG_POM    1u
#define RB_FLAG_NOPOM  2u
#define RB_FLAG_SPRITE 4u
/* DOOM-0183: liquid-identity bits set by FLAT NAME on the CPU (not CSV-parsed, so
 * rb_parse_flags never emits them and they can't collide). Mirror pathtrace.comp
 * MatCtrl.flags bit3/bit4: nukage gets the full wet treatment, lava glow only. */
#define RB_FLAG_LIQUID_NUKAGE 8u
#define RB_FLAG_LIQUID_LAVA   16u
/* Designed art, such as a computer panel: shuffling its cells to hide repeats would scramble it. */
#define RB_FLAG_NO_DETILE 32u
#define RB_FLAG_BAD    0x80000000u   /* unknown flags token — caller treats row malformed */

typedef struct {
    char          name[9];                     /* DOOM name, NUL-terminated (<=8 chars) */
    int           is_hero;                     /* 1 = hero, 0 = derive */
    char          maps[RB_MAP_COUNT][128];     /* per-map path; "" = no map */
    float         uv_scale;                    /* across the tile */
    float         uv_scale_v;                  /* up the tile */
    float         pom_depth;                   /* relief's full height in map units; 0 = default */
    unsigned int  flags;
} rb_matrow_t;

/* Trim leading/trailing ASCII whitespace in place; returns the trimmed start. */
static inline char* rb_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static inline unsigned int rb_parse_flags(const char* field, float* depth = nullptr) {
    unsigned int f = 0;
    char buf[128];
    if (strlen(field) >= sizeof(buf)) return RB_FLAG_BAD;   /* would truncate (DOOM-0410) */
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
                else if (!strcmp(tok, "noDetile")) f |= RB_FLAG_NO_DETILE;
                else if (!strncmp(tok, "depth=", 6)) {
                    /* A value, not a bit. Malformed unless a positive finite number. */
                    char* after;
                    const float d = strtof(tok + 6, &after);
                    if (after == tok + 6 || *after || !isfinite(d) || d <= 0.0f) f |= RB_FLAG_BAD;
                    else if (depth) *depth = d;
                }
                else                             f |= RB_FLAG_BAD;
            }
            if (end == '\0') break;
            start = c + 1;
        }
    }
    if (f & RB_FLAG_NOPOM) f &= ~RB_FLAG_POM;   /* noPom wins; shader checks one bit */
    return f;
}

/* DOOM-0553: did strtof read [start, end) as hex? Only a hex number has an 'x' in it. */
static inline int rb_has_x(const char* start, const char* end) {
    return end > start && (memchr(start, 'x', (size_t)(end - start))
                           || memchr(start, 'X', (size_t)(end - start)));
}

/* 1 = data row parsed, 0 = comment/blank (skip), -1 = malformed (log + skip).
   A field too long for its buffer is malformed, never truncated (DOOM-0410): a
   truncated path is a different, valid-looking path, and the row would then fail
   later as "no usable albedo", naming the wrong cause. */
static inline int rb_parse_material_line(const char* line, rb_matrow_t* out) {
    char buf[1024];
    /* The caller reads with fgets into a buffer of the same size, so a line it had
       to split arrives exactly full (1023 chars) with no newline. A full buffer that
       DOES end in '\n' is a complete line and fits. */
    const size_t len = strlen(line);
    if (len >= sizeof(buf)) return -1;                /* over-long line */
    if (len == sizeof(buf) - 1 && line[len - 1] != '\n') return -1;   /* split line */
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
    char* nm = rb_trim(col[0]);
    if (strlen(nm) > 8) return -1;              /* DOOM names are at most 8 chars */
    strncpy(out->name, nm, 8); out->name[8] = '\0';

    char* src = rb_trim(col[1]);
    if      (!strcmp(src, "hero"))   out->is_hero = 1;
    else if (!strcmp(src, "derive")) out->is_hero = 0;
    else return -1;                             /* unknown source */

    for (int i = 0; i < RB_MAP_COUNT; i++) {
        char* mp = rb_trim(col[2 + i]);
        if (strlen(mp) >= sizeof(out->maps[i])) return -1;   /* path would truncate */
        strncpy(out->maps[i], mp, sizeof(out->maps[i]) - 1);
    }

    /* Blank -> 1.0. Zero, negative, NaN and inf -> 1.0 too: NaN fails every
       comparison, so `<= 0` alone let it through to the shader's UV multiply
       (DOOM-0410). */
    /* DOOM-0553: each number must be read whole and in decimal. Junk after it ("2.0abc"), an
       empty second axis ("0.25x") or hex ("0x1p-2", which strtof reads as 0.25) is malformed.
       A hex number is the one strtof reads through an 'x'. */
    char* uvs = rb_trim(col[9]);
    char* after = uvs;
    out->uv_scale = strtof(uvs, &after);
    if (rb_has_x(uvs, after)) return -1;
    if (after == uvs && *uvs) return -1;
    if (!isfinite(out->uv_scale) || out->uv_scale <= 0.0f) out->uv_scale = 1.0f;
    /* "0.25x0.5" scales each axis on its own: art spanning four tiles wide and two tall. */
    if (*after == 'x') {
        char* v = after + 1;
        char* vend = v;
        out->uv_scale_v = strtof(v, &vend);
        if (vend == v || *vend || rb_has_x(v, vend)) return -1;
        if (!isfinite(out->uv_scale_v) || out->uv_scale_v <= 0.0f) out->uv_scale_v = 1.0f;
    } else {
        if (*after) return -1;
        out->uv_scale_v = out->uv_scale;
    }

    out->flags = rb_parse_flags(rb_trim(col[10]), &out->pom_depth);
    if (out->flags & RB_FLAG_BAD) return -1;    /* unknown flags token -> malformed */
    return 1;
}

/* GPU control struct — MUST match the std430 MatCtrl struct in pathtrace.comp.
   int maps[7]=28, then four 4-byte fields to 44, uint usePBR=48 (no padding). */
typedef struct {
    int           maps[RB_MAP_COUNT];
    float         uvScale;
    float         uvScaleV;
    float         pomDepth;
    unsigned int  flags;
    unsigned int  usePBR;
} rb_matctrl_t;

/* Writes up to max_ids unified ids for `name` and returns how many. More than one
   when the name is both a wall texture and a flat: the sidecar row applies to every
   match (DOOM-0410). 0 = not in this WAD. */
#define RB_MAX_NAME_IDS 2
typedef int (*rb_name_resolver_t)(const char* name, int* out_ids, int max_ids);

static inline void rb_build_ctrl_table(const rb_matrow_t* rows, int nrows, int nmaterials,
                         rb_name_resolver_t resolve, rb_matctrl_t* table, int* dup_count) {
    for (int i = 0; i < nmaterials; i++) {
        for (int m = 0; m < RB_MAP_COUNT; m++) table[i].maps[m] = -1;
        table[i].uvScale  = 1.0f;
        table[i].uvScaleV = 1.0f;
        table[i].pomDepth = 0.0f;
        table[i].flags    = 0u;
        table[i].usePBR   = 0u;                 /* default: paletted */
    }
    int dups = 0;
    for (int r = 0; r < nrows; r++) {
        int ids[RB_MAX_NAME_IDS];
        int nid = resolve(rows[r].name, ids, RB_MAX_NAME_IDS);   /* 0 = not in this WAD */
        for (int k = 0; k < nid; k++) {
            int id = ids[k];
            if (id < 0 || id >= nmaterials) continue;
            if (table[id].usePBR) dups++;            /* already set -> last-wins */
            for (int m = 0; m < RB_MAP_COUNT; m++) table[id].maps[m] = -1;  /* image load fills these */
            table[id].uvScale  = rows[r].uv_scale;
            table[id].uvScaleV = rows[r].uv_scale_v;
            table[id].pomDepth = rows[r].pom_depth;
            table[id].flags    = rows[r].flags;
            table[id].usePBR   = 1u;
        }
    }
    if (dup_count) *dup_count = dups;
}

typedef struct { int id; float traffic; int is_hero; } rb_sortent_t;

static inline int rb_cmp_ent(const void* a, const void* b) {
    const rb_sortent_t* x = (const rb_sortent_t*)a;
    const rb_sortent_t* y = (const rb_sortent_t*)b;
    if (x->is_hero != y->is_hero) return y->is_hero - x->is_hero;   /* heroes first */
    if (x->traffic < y->traffic) return 1;                          /* then desc traffic */
    if (x->traffic > y->traffic) return -1;
    return x->id - y->id;                                           /* stable tiebreak */
}

static inline void rb_apply_budget(rb_matctrl_t* table, int nmaterials,
                     const float* traffic, const float* est_mb, const int* is_hero,
                     float ceiling_mb, int* order_out, int* n_loaded) {
    rb_sortent_t* ent = (rb_sortent_t*)malloc((size_t)nmaterials * sizeof(rb_sortent_t));
    if (!ent) { *n_loaded = 0; return; }    /* OOM: load nothing, leave table as-is (never crash) */
    int m = 0;
    for (int i = 0; i < nmaterials; i++)
        if (table[i].usePBR) { ent[m].id = i; ent[m].traffic = traffic[i]; ent[m].is_hero = is_hero[i]; m++; }
    qsort(ent, m, sizeof(rb_sortent_t), rb_cmp_ent);

    float used = 0.0f;
    int n = 0;
    for (int k = 0; k < m; k++) {
        int id = ent[k].id;
        if (used + est_mb[id] <= ceiling_mb) {
            used += est_mb[id];
            order_out[n++] = id;                /* loaded, in upload order */
        } else {
            table[id].usePBR = 0;               /* over budget -> paletted */
        }
    }
    *n_loaded = n;
    free(ent);
}

/* Drop the materials this level never shows, before the budget counts them: without
   this every sidecar row is loaded on every level, and the ceiling fills with art no
   wall here uses (DOOM-0606). used[i] is non-zero when the level mesh names material i,
   at any size: a step that is closed at load is built with zero height, and still counts.
   exempt[i] marks a material the mesh cannot name, which is kept (a pickup model's atlas).
   Returns how many were dropped. A texture a switch or an animation swaps in later is not
   in the mesh at load, so an HD row for one would be dropped here; none exists today. */
static inline int rb_drop_unused(rb_matctrl_t* table, int nmaterials,
                                 const char* used, const char* exempt) {
    int dropped = 0;
    for (int i = 0; i < nmaterials; i++)
        if (table[i].usePBR && !used[i] && !exempt[i]) { table[i].usePBR = 0; dropped++; }
    return dropped;
}

/* The executable's directory, set once by the engine (rb_asset_set_exe_dir) so the
   default root does not depend on where the game was launched from (DOOM-0410).
   Empty until set, which is the state the unit tests run in. */
static char rb_asset_exe_root[512];

/* Given the executable's directory (with a trailing separator, as SDL_GetBasePath
   returns it), adopt the first of these that really holds materials.csv: <exe>/assets/ultra/,
   a download's layout (DOOM-0498), then <exe>/../../assets/ultra/, the source tree, where
   the binary is linuxdoom-1.10/linux/. Returns 1 if one was adopted. */
static inline int rb_asset_set_exe_dir(const char* exe_dir) {
    static const char* const kCandidates[] = { "%sassets/ultra/", "%s../../assets/ultra/" };
    char cand[512], csv[600];
    rb_asset_exe_root[0] = '\0';
    if (!exe_dir || !*exe_dir) return 0;
    for (const char* const fmt : kCandidates) {
        if (snprintf(cand, sizeof cand, fmt, exe_dir) >= (int)sizeof cand) continue;
        snprintf(csv, sizeof csv, "%smaterials.csv", cand);
        FILE* f = fopen(csv, "r");
        if (!f) continue;
        fclose(f);
        strcpy(rb_asset_exe_root, cand);
        return 1;
    }
    return 0;
}

/* Asset root for HD material files, first match wins: $DOOMASSETDIR; the art found
   from the executable (rb_asset_set_exe_dir); "assets/ultra/" relative to
   the CWD (mirrors how the WAD is found via DOOMWADDIR). */
static inline const char* rb_asset_root(void) {
    const char* e = getenv("DOOMASSETDIR");
    if (e && *e) return e;
    if (rb_asset_exe_root[0]) return rb_asset_exe_root;
    return "assets/ultra/";
}

/* Join the asset root and a relative path into dst (e.g. root + "materials.csv").
   Returns 1, or 0 if the result would not fit (dst then holds nothing usable). */
static inline int rb_asset_path(char* dst, int dstsz, const char* rel) {
    const char* root = rb_asset_root();
    size_t n = strlen(root);
    int need_slash = (n > 0 && root[n-1] != '/');
    int w = snprintf(dst, dstsz, "%s%s%s", root, need_slash ? "/" : "", rel);
    if (w < 0 || w >= dstsz) { if (dstsz > 0) dst[0] = '\0'; return 0; }
    return 1;
}

#endif
