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

#endif // NEE_SAMPLING_H
