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
#ifndef NEE_SAMPLING_H
#define NEE_SAMPLING_H

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

// Merge the cached STATIC emitter records with this frame's DYNAMIC (sprite) records
// into the GPU emitter buffer `out`, filling each record's power-sampling (cdf,pdf)
// table (slots 12,13) from its weight (luminance(Le)*area, precomputed by the caller).
//
// The ordering is load-bearing: STATIC records occupy [0, staticN) and DYNAMIC records
// occupy [staticN, n). That boundary IS the shader's `omniStart` (pathtrace.comp treats
// index >= omniStart as an omnidirectional sprite light, index < omniStart as an
// oriented wall/flat light), so this split and the host's misc4.y MUST agree — if they
// drift, sprites get mislabelled and lamps stop casting. n = min(staticN + dynN, cap);
// dynamic overflow past the cap is dropped (never a static light). `out` must hold
// cap*NEE_EMIT_STRIDE floats. Returns n (the emitter count the shader iterates).
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

#endif // NEE_SAMPLING_H
