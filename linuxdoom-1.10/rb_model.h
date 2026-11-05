// rb_model.h — DOOM-0539: read a pickup model's .dmdl file (spec §4.2).
//
// Header-only so a test reaches it without the engine. Models come from a folder the user
// can set (DOOMASSETDIR), so a file is untrusted: every count is capped before it is used
// for a size, the size must then match exactly, and every float must be finite and in its
// range (INV-4).
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
// DOOM-0547: ranges a finite value must also keep. A pickup is tens of units across, a light's
// colour is near 0..1 and its reach under a few hundred units; the caps leave room above that.
#define RB_MODEL_MAX_EXTENT    128.0f  // |x|, |y|, |z| of a vertex or a light
#define RB_MODEL_MAX_LIGHT_RGB 16.0f
#define RB_MODEL_MAX_REACH     1024.0f
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
    // DOOM-0547: in range too, or a finite value near FLT_MAX overflows the vertex buffer and a
    // light's gain overflows the emitter weights.
    for (i = 0; i < nv; i++)
    {
        const float* v = floats + i * RB_MODEL_VERT_FLOATS;
        const float  n2 = v[3] * v[3] + v[4] * v[4] + v[5] * v[5];
        if (fabsf(v[0]) > RB_MODEL_MAX_EXTENT || fabsf(v[1]) > RB_MODEL_MAX_EXTENT
            || fabsf(v[2]) > RB_MODEL_MAX_EXTENT
            || n2 < 0.81f || n2 > 1.21f  // length within 0.9..1.1
            || v[6] < 0.0f || v[6] > 1.0f || v[7] < 0.0f || v[7] > 1.0f)
        {
            free(floats);
            return 0;
        }
    }
    for (i = 0; i < nl; i++)
    {
        const float* l = floats + (size_t)nv * RB_MODEL_VERT_FLOATS + i * RB_MODEL_LIGHT_FLOATS;
        if (fabsf(l[0]) > RB_MODEL_MAX_EXTENT || fabsf(l[1]) > RB_MODEL_MAX_EXTENT
            || fabsf(l[2]) > RB_MODEL_MAX_EXTENT
            || l[3] < 0.0f || l[4] < 0.0f || l[5] < 0.0f || l[3] > RB_MODEL_MAX_LIGHT_RGB
            || l[4] > RB_MODEL_MAX_LIGHT_RGB || l[5] > RB_MODEL_MAX_LIGHT_RGB
            || !(l[6] > 0.0f) || l[6] > RB_MODEL_MAX_REACH)
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
