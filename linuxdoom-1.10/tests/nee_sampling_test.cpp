// nee_sampling_test.cpp — DOOM-0009 path tracer, build step 3c-3 (INV-6 part 1).
//
// Proves the build-step-3c-2 power-importance emitter sampling is UNBIASED: that
// the (cdf, pdf) table built by nee_build_cdf + the nee_pick binary search (the
// exact logic pathtrace.comp runs on the GPU) select emitter k with frequency
// equal to its stored pdf[k], and that the next-event estimator g(k)/pdf(k) has
// the same expected value as uniform sampling — i.e. power sampling only cut
// variance, it did not move the answer.
//
// This is the direct, deterministic proof of the one property 3c-2 put at risk.
// The geometry term (cos/cos/dist^2, area, shadow occlusion) is unchanged from
// 3c-1 and was verified on-screen; this test isolates the new selection+weight.
// The full GPU image rel-MSE regression vs a brute-force Cornell reference (the
// rest of spec INV-6) lands at build step 4, when the integrator is complete and
// a high-precision off-screen render target exists.
//
// Build/run: `make nee-test` (from linuxdoom-1.10/). No WAD or GPU needed.
#include "../nee_sampling.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

// --- the shader's PRNG, ported verbatim from pathtrace.comp -------------------
// Using the real GPU uniform source makes the CPU test faithful to the device.
static uint32_t pcgHash(uint32_t x)
{
    uint32_t state = x * 747796405u + 2891336453u;
    uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
static float rnd(uint32_t& s) { s = pcgHash(s); return (float)s * (1.0f / 4294967296.0f); }

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); g_failures++; }
}

// Structural invariants of the table: pdf is a normalised distribution, cdf is its
// monotone running sum reaching exactly 1, and pdf[i] = cdf[i] - cdf[i-1].
static void check_table(const std::vector<float>& w)
{
    const int n = (int)w.size();
    std::vector<float> cdf(n), pdf(n);
    nee_build_cdf(w.data(), n, cdf.data(), pdf.data());

    double sum = 0.0;
    bool monotone = true, pdf_nonneg = true, pdf_matches_cdf = true;
    for (int i = 0; i < n; i++)
    {
        sum += pdf[i];
        if (pdf[i] < 0.0f) pdf_nonneg = false;
        if (i > 0 && cdf[i] < cdf[i - 1]) monotone = false;
        float edge = (i == 0) ? cdf[0] : (cdf[i] - cdf[i - 1]);
        if (std::fabs(edge - pdf[i]) > 1e-5f) pdf_matches_cdf = false;
    }
    check(pdf_nonneg, "pdf entries are non-negative");
    check(monotone, "cdf is monotone non-decreasing");
    check(std::fabs(sum - 1.0) < 1e-4, "pdf sums to 1");
    check(std::fabs(cdf[n - 1] - 1.0f) < 1e-6f, "cdf upper edge is exactly 1");
    check(pdf_matches_cdf, "pdf[i] == cdf[i] - cdf[i-1]");
}

// Draw N picks through nee_pick and confirm the realised frequency of each index
// matches its pdf to within Monte-Carlo error — the core unbiasedness condition
// (sample k with probability pdf[k]). Also confirms the importance-sampling
// estimator mean(g[k]/pdf[k]) equals sum(g) (and so equals the uniform-sampling
// estimate count*mean_uniform(g)), for two integrands: g==w (matched -> near zero
// variance: every sample equals the total) and a g uncorrelated with w (general).
static void check_distribution(const std::vector<float>& w, const char* label)
{
    const int n = (int)w.size();
    std::vector<float> cdf(n), pdf(n);
    nee_build_cdf(w.data(), n, cdf.data(), pdf.data());

    // A general integrand, deliberately NOT proportional to the weights.
    std::vector<float> g(n);
    double trueSumG = 0.0, trueSumW = 0.0;
    for (int i = 0; i < n; i++)
    {
        g[i] = (float)((i * 37 + 11) % 100 + 1) * 0.1f;   // arbitrary, positive
        trueSumG += g[i];
        trueSumW += w[i];
    }

    const long N = 16000000;
    std::vector<long> hist(n, 0);
    double estG = 0.0, estW = 0.0;   // power-sampled estimators of sum(g), sum(w)
    uint32_t seed = 0x1234567u;
    for (long s = 0; s < N; s++)
    {
        float u = rnd(seed);
        int k = nee_pick(cdf.data(), n, u);
        if (k < 0 || k >= n) { check(false, "nee_pick returned out-of-range index"); return; }
        hist[k]++;
        double p = (double)pdf[k];
        if (p > 0.0) { estG += (double)g[k] / p; estW += (double)w[k] / p; }
    }
    estG /= (double)N;
    estW /= (double)N;

    // 1) Realised frequency matches pdf within 6 sigma (binomial), for every index.
    bool freqOk = true;
    for (int i = 0; i < n; i++)
    {
        double freq = (double)hist[i] / (double)N;
        double sigma = std::sqrt(std::fmax(pdf[i] * (1.0 - pdf[i]) / (double)N, 1e-18));
        if (std::fabs(freq - pdf[i]) > 6.0 * sigma + 1e-7) freqOk = false;
    }
    check(freqOk, "selection frequency matches pdf (<=6 sigma) for all indices");

    // 2) Estimator is unbiased: power-sampled mean(g/pdf) -> sum(g) within 0.5%.
    if (trueSumG > 0.0)
        check(std::fabs(estG - trueSumG) / trueSumG < 0.005,
              "power-sampled estimator of sum(g) within 0.5% (unbiased)");

    // 3) Matched integrand g==w is the importance-sampling sweet spot: each sample
    //    of w/pdf equals the total, so the estimate hits sum(w) to float precision.
    if (trueSumW > 0.0)
        check(std::fabs(estW - trueSumW) / trueSumW < 1e-3,
              "matched estimator of sum(w) is near-exact (variance ~0)");

    std::printf("  [%s] n=%d  est sum(g)=%.4f (true %.4f)  est sum(w)=%.4f (true %.4f)\n",
                label, n, estG, trueSumG, estW, trueSumW);
}

int main()
{
    std::printf("nee_sampling_test (DOOM-0009 step 3c-3): power-importance NEE is unbiased\n");

    // A spread of weight distributions an emitter list can produce.
    std::vector<std::vector<float>> sets = {
        {1.0f},                                            // single emitter
        {1.0f, 1.0f, 1.0f, 1.0f},                          // uniform weights
        {100.0f, 1.0f, 1.0f, 1.0f, 1.0f},                  // one dominant light
        {0.0f, 0.0f, 0.0f},                                // all-zero -> uniform fallback
        {0.01f, 0.5f, 3.0f, 12.0f, 40.0f, 0.2f, 7.0f, 1.0f},  // realistic skew
    };
    const char* names[] = { "single", "uniform", "dominant", "all-zero", "skewed" };

    for (size_t i = 0; i < sets.size(); i++)
    {
        std::printf("- weight set: %s\n", names[i]);
        check_table(sets[i]);
        check_distribution(sets[i], names[i]);
    }

    if (g_failures == 0)
        std::printf("PASS: all checks green — 3c-2 power sampling is unbiased.\n");
    else
        std::printf("FAIL: %d check(s) failed.\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
