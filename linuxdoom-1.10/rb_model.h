// rb_model.h — DOOM-0539: read a pickup model's .dmdl file (spec §4.2).
//
// Header-only so a test reaches it without the engine. Models come from a folder the user
// can set (DOOMASSETDIR), so a file is untrusted: every count is capped before it is used
// for a size, the size must then match exactly, and every float must be finite (INV-4).
#ifndef RB_MODEL_H
#define RB_MODEL_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RB_MODEL_VERSION     1u
#define RB_MODEL_MAX_VERTS   900u  // three per triangle, so 300 triangles
#define RB_MODEL_MAX_LIGHTS  4u
#define RB_MODEL_VERT_FLOATS 8u    // x y z, nx ny nz, u v
#define RB_MODEL_LIGHT_FLOATS 7u   // x y z, r g b, reach
#define RB_MODEL_HEADER_SIZE 16u
#define RB_MODEL_MAX_FILE    (RB_MODEL_HEADER_SIZE + (RB_MODEL_MAX_VERTS * RB_MODEL_VERT_FLOATS \
                              + RB_MODEL_MAX_LIGHTS * RB_MODEL_LIGHT_FLOATS) * 4u)

typedef struct
{
    uint32_t numVerts;
    uint32_t numLights;
    float*   floats;  // numVerts vertices, then numLights lights; one heap block
} rb_model_t;

static inline const float* RB_ModelVert(const rb_model_t* m, uint32_t i)
{
    return m->floats + i * RB_MODEL_VERT_FLOATS;
}

static inline const float* RB_ModelLight(const rb_model_t* m, uint32_t i)
{
    return m->floats + m->numVerts * RB_MODEL_VERT_FLOATS + i * RB_MODEL_LIGHT_FLOATS;
}

// Fills *out and returns 1, or returns 0 and leaves *out empty. The caller frees a filled
// model with RB_ModelFree. The body is copied, so `data` need not be aligned or kept.
static inline int RB_ModelParse(const void* data, size_t size, rb_model_t* out)
{
    const unsigned char* b = (const unsigned char*)data;
    uint32_t version, nv, nl;
    size_t   count, i;
    float*   floats;

    out->numVerts = out->numLights = 0;
    out->floats = NULL;

    if (!b || size < RB_MODEL_HEADER_SIZE || memcmp(b, "DMDL", 4) != 0)
        return 0;
    memcpy(&version, b + 4, 4);
    memcpy(&nv, b + 8, 4);
    memcpy(&nl, b + 12, 4);
    if (version != RB_MODEL_VERSION)
        return 0;
    if (nv < 3u || nv > RB_MODEL_MAX_VERTS || nv % 3u != 0u || nl > RB_MODEL_MAX_LIGHTS)
        return 0;

    count = (size_t)nv * RB_MODEL_VERT_FLOATS + (size_t)nl * RB_MODEL_LIGHT_FLOATS;
    if (size != RB_MODEL_HEADER_SIZE + count * sizeof(float))
        return 0;

    floats = (float*)malloc(count * sizeof(float));
    if (!floats)
        return 0;
    memcpy(floats, b + RB_MODEL_HEADER_SIZE, count * sizeof(float));
    for (i = 0; i < count; i++)
    {
        if (!isfinite(floats[i]))
        {
            free(floats);
            return 0;
        }
    }

    out->numVerts = nv;
    out->numLights = nl;
    out->floats = floats;
    return 1;
}

static inline void RB_ModelFree(rb_model_t* m)
{
    free(m->floats);
    m->floats = NULL;
    m->numVerts = m->numLights = 0;
}

// Reads a whole .dmdl from an open file. One byte past the largest valid file is read, so
// an oversized one fails the parser's exact size check without being held in memory.
static inline int RB_ModelRead(FILE* f, rb_model_t* out)
{
    unsigned char buf[RB_MODEL_MAX_FILE + 1];
    return RB_ModelParse(buf, fread(buf, 1, sizeof buf, f), out);
}

// One line of models.csv, `sprite,file`. Returns 1 for a row, 0 for a comment or blank line,
// -1 for anything else. A file path too long for its buffer is malformed, never truncated.
static inline int RB_ModelCsvLine(const char* line, char sprite[5], char* file, size_t filesz)
{
    const char* comma;
    size_t      len;

    line += strspn(line, " \t\r\n");
    if (!*line || *line == '#')
        return 0;
    comma = strchr(line, ',');
    if (!comma || comma - line != 4)
        return -1;
    memcpy(sprite, line, 4);
    sprite[4] = '\0';
    len = strcspn(comma + 1, " \t\r\n");
    if (len == 0 || len >= filesz || (comma + 1)[len + strspn(comma + 1 + len, " \t\r\n")])
        return -1;
    memcpy(file, comma + 1, len);
    file[len] = '\0';
    return 1;
}

#endif
