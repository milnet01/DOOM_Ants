// nee_sampling.h — DOOM-0009 path tracer, build step 3c-2.
//
// The power-importance emitter-sampling math, factored out so the ONE
// implementation is shared by:
//   • the CPU emitter-list builder (r_vulkan.cpp BuildEmitterList), which bakes
//     the per-emitter (cdf, pdf) table into the GPU emitter buffer, and
//   • the unbiasedness test (tests/nee_sampling_test.cpp), which proves the
//     table + selection reproduce the intended distribution exactly.
// nee_pick mirrors the shader's binary search (pathtrace.comp) byte-for-byte in
// logic, so the test exercises the algorithm that actually runs on the GPU.
//
// Why this proves unbiasedness: next-event estimation picks emitter k with
// probability pdf[k] and weights its contribution by 1/pdf[k]. The Monte-Carlo
// identity E[g(k)/pdf(k)] = sum_k g(k) holds for ANY pdf with pdf[k] > 0 where a
// non-zero g[k] can be chosen — so power sampling has the SAME expected value as
// uniform sampling, only lower variance. The only way 3c-2 could bias the image
// is if the realised selection frequency does not match the stored pdf, or the
// pdf is not a normalised distribution. Both are exactly what the test checks.
//
// DOOM-0653 adds the traced view's per-cell mixture pick of the static set (below
// nee_pick_static), which keeps the global draw as one of its two strategies.
#ifndef NEE_SAMPLING_H
#define NEE_SAMPLING_H

#include <cstdint>
#include <cstring>
#include <vector>

// Floats per emitter record in the GPU buffer: v0[3] v1[3] v2[3] Le[3] cdf pdf.
static const int NEE_EMIT_STRIDE = 14;

// Build the cumulative (cdf) + per-item (pdf) power-sampling table from a set of
// non-negative weights (weight = luminance(Le) * triangle area). cdf[i] is the
// inclusive upper edge of item i's interval in [0,1]; pdf[i] = cdf[i]-cdf[i-1].
// If every weight is zero (degenerate geometry), falls back to a uniform table
// so the shader's 1/pdf divide is always finite. cdf/pdf must hold `count` floats.
static inline void nee_build_cdf(const float* w, int count, float* cdf, float* pdf)
{
    double total = 0.0;
    for (int i = 0; i < count; i++) total += (double)w[i];

    double acc = 0.0;
    for (int i = 0; i < count; i++)
    {
        float p = total > 0.0 ? (float)((double)w[i] / total) : 1.0f / (float)count;
        acc += (double)p;
        cdf[i] = total > 0.0 ? (float)acc : (float)(i + 1) / (float)count;
        pdf[i] = p;
    }
    if (count > 0)
        cdf[count - 1] = 1.0f;   // exact upper edge so a u just below 1 still lands
}

// Pick an item by binary-searching the cdf for the first index whose upper edge
// is >= u (u uniform in [0,1)). Identical control flow to pathtrace.comp's loop.
//
// PRECONDITION: count >= 1. With count == 0 this returns 0, an index into an empty
// table, and the caller reads a slot that does not exist. It is NOT guarded here on
// purpose: this function exists to mirror the shader's loop byte for byte, which is
// what lets the unit test speak for the shader, and a guard on one side only would
// end that. The shader's protection is upstream -- it does not run next-event
// estimation with no emitters -- and a C++ caller owes the same check. Recorded
// rather than fixed (DOOM-0406).
static inline int nee_pick(const float* cdf, int count, float u)
{
    int lo = 0, hi = count - 1;
    while (lo < hi)
    {
        int mid = (lo + hi) >> 1;
        if (cdf[mid] < u) lo = mid + 1;
        else              hi = mid;
    }
    return lo;
}

// DOOM-0377: the static-prefix draw, as shadeSurface (pt_common.glsl) makes it.
//
// The table is built over the MERGED set -- static emitters, then this frame's
// sprite lights -- but the shader samples only the static prefix [0, omniStart)
// through it; the sprite lights have their own loop. The prefix's cdf tops out
// at 1 - P_omni, not 1. Searching it with a u drawn over [0,1) and dividing by
// the full-set pdf sent every u above that top to the last static emitter at
// full weight, so that one emitter was counted a second time, scaled by
// P_omni / pdf -- unbounded as its pdf shrinks. It showed on any frame with a
// glowing sprite in view and vanished without one, which is why the bake was
// clean and the camera path was not.
//
// So the prefix is renormalised: u is drawn over [0, cdfMax) and the pdf is
// divided by cdfMax, where cdfMax is the prefix's last cdf value. With no sprite
// lights cdfMax is 1 and nothing changes.
//
// PRECONDITION: omniStart >= 1, as for nee_pick. `u01` is uniform in [0,1).
static inline int nee_pick_static(const float* cdf, const float* pdf, int omniStart,
                                  float u01, float* pdfSel)
{
    const float cdfMax = cdf[omniStart - 1];
    const int   k      = nee_pick(cdf, omniStart, u01 * cdfMax);

    *pdfSel = pdf[k] / (cdfMax > 1e-8f ? cdfMax : 1e-8f);
    return k;
}

// ---------------------------------------------------------------------------------------
// DOOM-0653: the traced view's per-cell pick of the static set, a MIXTURE of two draws.
// With probability a = NEE_GLOBAL_FRAC a sample makes the global draw above; otherwise it
// draws a light cluster from the hit's cell list (by its estimate at the cell), then a
// triangle in that cluster (by power). Either way it is weighted by
//     p(t) = a * pg(t) + (1 - a) * pc(cluster(t)) * pin(t)
// so the estimate stays unbiased whatever a list leaves out: a light missing from the
// list is still reached through the global share (spec INV-1, INV-2). A cell with no
// list, or no cell (the GI bake), sets a = 1, which is nee_pick_static exactly (INV-4).
//
// The lists' buffer, which lgrid::BuildNeeCellLists (light_grid.h) writes and
// pt_common.glsl's neePickStatic reads, is uint32 words end to end, floats bit-cast:
//   header   NEE_HDR_WORDS words, named below
//   cells    two per cell: first entry, count
//   entries  NEE_ENTRY_WORDS each: cluster id, cdf, pdf; a cell's sorted by cluster id
//   clusters two per cluster: first triangle, count
//   tris     two per triangle, grouped by cluster: emitter index, in-cluster cdf
//   emit     two per static emitter: cluster id (NEE_NO_CLUSTER for none), in-cluster pdf
// Each name below MUST equal pt_common.glsl's (shader_mirror_test.cpp).
static const float    NEE_GLOBAL_FRAC    = 0.25f;
static const uint32_t NEE_HDR_CELLS      = 0;   // cell count (the seep grid's)
static const uint32_t NEE_HDR_EMITTERS   = 1;   // the static emitter count the lists index
static const uint32_t NEE_HDR_CELL_AT    = 2;   // word offsets of each section
static const uint32_t NEE_HDR_ENTRY_AT   = 3;
static const uint32_t NEE_HDR_CLUSTER_AT = 4;
static const uint32_t NEE_HDR_TRI_AT     = 5;
static const uint32_t NEE_HDR_EMIT_AT    = 6;
static const uint32_t NEE_HDR_CLUSTERS   = 7;   // cluster count
static const uint32_t NEE_HDR_WORDS      = 8;
static const uint32_t NEE_ENTRY_WORDS    = 3;
static const uint32_t NEE_NO_CLUSTER     = 0xFFFFFFFFu;

static inline float nee_word_float(uint32_t u) { float f; std::memcpy(&f, &u, sizeof f); return f; }

// Cell `cell`'s list in `nee` as (first entry, count); count 0 when the buffer holds no
// lists for this static set, or the cell is outside it. Matches neeCellList.
static inline uint32_t nee_cell_list(const uint32_t* nee, int cell, int omniStart, uint32_t* first)
{
    *first = 0;
    if (!nee || cell < 0 || nee[NEE_HDR_EMITTERS] != (uint32_t)omniStart
        || (uint32_t)cell >= nee[NEE_HDR_CELLS])
        return 0;
    const uint32_t* c = nee + nee[NEE_HDR_CELL_AT] + 2u * (uint32_t)cell;
    *first = c[0];
    return c[1];
}

// The mixture pdf of static emitter k for a hit whose list is (first, count). `pg` is k's
// global pdf over the static prefix (pdf[k] / cdfMax). Matches neeMixturePdf.
static inline float nee_mixture_pdf(const uint32_t* nee, uint32_t first, uint32_t count,
                                    int k, float pg)
{
    if (count == 0)
        return pg;
    const uint32_t* em = nee + nee[NEE_HDR_EMIT_AT] + 2u * (uint32_t)k;
    float pc = 0.0f;
    if (em[0] != NEE_NO_CLUSTER)
    {
        // The entries are sorted by cluster id: binary-search for k's cluster.
        const uint32_t* en = nee + nee[NEE_HDR_ENTRY_AT];
        uint32_t lo = first, hi = first + count;
        while (lo < hi)
        {
            const uint32_t mid = (lo + hi) >> 1;
            if (en[mid * NEE_ENTRY_WORDS] < em[0]) lo = mid + 1;
            else                                   hi = mid;
        }
        if (lo < first + count && en[lo * NEE_ENTRY_WORDS] == em[0])
            pc = nee_word_float(en[lo * NEE_ENTRY_WORDS + 2]) * nee_word_float(em[1]);
    }
    return NEE_GLOBAL_FRAC * pg + (1.0f - NEE_GLOBAL_FRAC) * pc;
}

// The mixture pick, as neePickStatic (pt_common.glsl) makes it. u0 and u1 are uniform in
// [0,1); u1 is read only on the cluster draw (the shader draws it only there). With no
// list this is nee_pick_static(cdf, pdf, omniStart, u0, pdfSel) exactly.
// PRECONDITION: omniStart >= 1, as for nee_pick_static.
static inline int nee_pick_mixture(const float* cdf, const float* pdf, int omniStart,
                                   const uint32_t* nee, int cell, float u0, float u1,
                                   float* pdfSel)
{
    const float cdfMax = cdf[omniStart - 1];
    const float inv    = 1.0f / (cdfMax > 1e-8f ? cdfMax : 1e-8f);
    uint32_t first;
    const uint32_t count = nee_cell_list(nee, cell, omniStart, &first);
    const float a = count ? NEE_GLOBAL_FRAC : 1.0f;
    int k;
    if (count == 0 || u0 < a)                 // u0 may be exactly 1, as the shader's rnd can be
        k = nee_pick(cdf, omniStart, (u0 / a) * cdfMax);
    else
    {
        // A cluster from the cell's cdf, then a triangle from the cluster's.
        const float     u  = (u0 - a) / (1.0f - a);
        const uint32_t* en = nee + nee[NEE_HDR_ENTRY_AT];
        uint32_t lo = first, hi = first + count - 1;
        while (lo < hi)
        {
            const uint32_t mid = (lo + hi) >> 1;
            if (nee_word_float(en[mid * NEE_ENTRY_WORDS + 1]) < u) lo = mid + 1;
            else                                                   hi = mid;
        }
        const uint32_t* cl = nee + nee[NEE_HDR_CLUSTER_AT] + 2u * en[lo * NEE_ENTRY_WORDS];
        const uint32_t* tr = nee + nee[NEE_HDR_TRI_AT];
        uint32_t tlo = cl[0], thi = cl[0] + cl[1] - 1;
        while (tlo < thi)
        {
            const uint32_t mid = (tlo + thi) >> 1;
            if (nee_word_float(tr[2 * mid + 1]) < u1) tlo = mid + 1;
            else                                      thi = mid;
        }
        k = (int)tr[2 * tlo];
    }
    *pdfSel = nee_mixture_pdf(nee, first, count, k, pdf[k] * inv);
    return k;
}

// Merge the cached STATIC emitter records with this frame's DYNAMIC (sprite) records
// into the GPU emitter buffer `out`, filling each record's power-sampling (cdf,pdf)
// table (slots 12,13) from its weight (luminance(Le)*area, precomputed by the caller).
//
// The ordering is load-bearing: STATIC records occupy [0, staticN) and DYNAMIC records
// occupy [staticN, n). That boundary IS the shader's `omniStart` (pathtrace.comp treats
// index >= omniStart as an omnidirectional sprite light, index < omniStart as an
// oriented wall/flat light), so this split and the host's misc4.y MUST agree — if they
// drift, sprites get mislabelled and lamps stop casting. n = min(staticN + dynN, cap).
//
// Overflow past the cap is dropped DYNAMIC-FIRST, because the static records are
// copied first and the copy stops at n. That is the intended policy and holds
// whenever staticN <= cap. It does NOT hold when staticN alone exceeds cap: n is
// then cap, the static copy itself stops short, and STATIC lights are dropped too
// -- lamps, which is the worst thing to lose. The comment here claimed static
// records were never dropped, which is why this says otherwise (DOOM-0406).
// Nothing reports either drop.
//
// `out` must hold cap*NEE_EMIT_STRIDE floats. Returns n (the emitter count the
// shader iterates).
//
// Pure (no GPU state) precisely so r_vulkan.cpp's FinalizeEmitters and the unit test
// in tests/nee_sampling_test.cpp share this one implementation — the ordering can't
// silently rot without the test going red.
static inline int nee_merge_emitters(const float* staticRec, const float* staticWgt, int staticN,
                                     const float* dynRec,    const float* dynWgt,    int dynN,
                                     int cap, float* out)
{
    if (staticN < 0) staticN = 0;
    if (dynN    < 0) dynN    = 0;
    int n = staticN + dynN;
    if (n > cap) n = cap;                 // drop excess sprites past the buffer cap
    if (n <= 0) return 0;

    std::vector<float> wgt(n), cdf(n), pdf(n);
    for (int i = 0; i < staticN && i < n; i++)        wgt[i]            = staticWgt[i];
    for (int i = 0; i < dynN && staticN + i < n; i++) wgt[staticN + i]  = dynWgt[i];
    nee_build_cdf(wgt.data(), n, cdf.data(), pdf.data());

    int w = 0;
    for (int i = 0; i < staticN && w < n; i++, w++)
        for (int f = 0; f < NEE_EMIT_STRIDE; f++) out[w * NEE_EMIT_STRIDE + f] = staticRec[i * NEE_EMIT_STRIDE + f];
    for (int i = 0; i < dynN && w < n; i++, w++)
        for (int f = 0; f < NEE_EMIT_STRIDE; f++) out[w * NEE_EMIT_STRIDE + f] = dynRec[i * NEE_EMIT_STRIDE + f];
    for (int i = 0; i < n; i++) { out[i * NEE_EMIT_STRIDE + 12] = cdf[i]; out[i * NEE_EMIT_STRIDE + 13] = pdf[i]; }
    return n;
}

// The static|omni split the shader is told (its `omniStart`): emitter records
// [0, omniStart) are the oriented static set it binary-searches, and
// [omniStart, mergedN) the omnidirectional sprite lights.
//
// It is the static count -- but never more than the records nee_merge_emitters
// actually wrote. The static set is rebuilt on a texture change and can grow past
// the buffer's cap; the merge then writes `cap` records, and a split pushed as the
// raw static count sent the shader's search past the end of the buffer (DOOM-0093).
// `mergedN` is nee_merge_emitters' return value.
static inline int nee_omni_start(int staticN, int mergedN)
{
    if (mergedN < 0) mergedN = 0;
    if (staticN < 0) staticN = 0;
    return staticN < mergedN ? staticN : mergedN;
}

#endif // NEE_SAMPLING_H
