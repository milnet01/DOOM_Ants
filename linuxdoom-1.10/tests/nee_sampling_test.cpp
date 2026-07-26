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
// Build/run: `make test` (from linuxdoom-1.10/) runs this with the rest of the
// suite; `make nee-test` builds and runs just this one for a fast iteration
// loop. No WAD or GPU needed.
#include "../nee_sampling.h"
#include "check_util.h"

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

    // Exact standard error of the importance-sampling estimator X = g[k]/pdf[k]:
    //   Var(X) = sum_i g[i]^2 / pdf[i] - (sum g)^2,  SE = sqrt(Var/N)
    // Computed from the table, not from the samples, so the bound below is a
    // property of the distribution rather than of this run. The 2026-07-26 test
    // audit measured the old flat 0.5% tolerance at only ~3.9 SE for the
    // "dominant" weight set and ~4.1 SE for "skewed" — far short of the 6 sigma
    // the frequency check above advertises, and a bound that would silently
    // tighten further if a future edit changed N, the weights, or g.
    double varG = 0.0;
    for (int i = 0; i < n; i++)
        if (pdf[i] > 0.0f) varG += (double)g[i] * (double)g[i] / (double)pdf[i];
    varG -= trueSumG * trueSumG;
    const double seG = std::sqrt(std::fmax(varG, 0.0) / (double)N);

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

    // 2) Estimator is unbiased: power-sampled mean(g/pdf) -> sum(g) within the
    //    6-sigma Monte-Carlo error derived above (1e-9 floor for the n==1 case,
    //    where the estimator is exact and SE is 0).
    if (trueSumG > 0.0)
        check(std::fabs(estG - trueSumG) <= 6.0 * seG + 1e-9,
              "power-sampled estimator of sum(g) within 6 sigma (unbiased)");

    // 3) Matched integrand g==w is the importance-sampling sweet spot: each sample
    //    of w/pdf equals the total, so the estimate hits sum(w) to float precision.
    if (trueSumW > 0.0)
        check(std::fabs(estW - trueSumW) / trueSumW < 1e-3,
              "matched estimator of sum(w) is near-exact (variance ~0)");

    std::printf("  [%s] n=%d  est sum(g)=%.4f (true %.4f, %.2f sigma out)  est sum(w)=%.4f (true %.4f)\n",
                label, n, estG, trueSumG,
                seG > 0.0 ? std::fabs(estG - trueSumG) / seG : 0.0, estW, trueSumW);
}

// Lock the static|sprite emitter split (DOOM-0084). nee_merge_emitters() — the exact
// merge r_vulkan.cpp's FinalizeEmitters runs — must place every STATIC record before
// every DYNAMIC (sprite) record, because that boundary IS the shader's omniStart
// (index >= omniStart -> omnidirectional sprite light). If a future edit reorders or
// interleaves them, sprites get mislabelled and lamps stop casting (the exact bug this
// guards). Also checks the cap drops sprites, never static lights, and the cdf is built.
static void check_merge_ordering()
{
    const int S = NEE_EMIT_STRIDE;
    const int staticN = 3, dynN = 2;
    // Tag slot 0 of each record with a unique id (static 10x, sprite 20x) so the
    // post-merge order is checkable; give sprites a smaller weight than statics.
    std::vector<float> sRec(staticN * S, 0.0f), sWgt(staticN);
    std::vector<float> dRec(dynN * S, 0.0f),    dWgt(dynN);
    for (int i = 0; i < staticN; i++) { sRec[i * S + 0] = 100.0f + (float)i; sWgt[i] = 1.0f + (float)i; }
    for (int i = 0; i < dynN;    i++) { dRec[i * S + 0] = 200.0f + (float)i; dWgt[i] = 0.5f; }

    std::vector<float> out((staticN + dynN) * S, -1.0f);
    int n = nee_merge_emitters(sRec.data(), sWgt.data(), staticN,
                               dRec.data(), dWgt.data(), dynN,
                               staticN + dynN, out.data());
    check(n == staticN + dynN, "merge returns staticN + dynN when under cap");

    bool orderOk = (n == staticN + dynN);
    for (int i = 0; i < staticN; i++) if (out[i * S + 0] != 100.0f + (float)i) orderOk = false;
    for (int i = 0; i < dynN;    i++) if (out[(staticN + i) * S + 0] != 200.0f + (float)i) orderOk = false;
    check(orderOk, "static records fill [0,staticN), sprites fill [staticN,n) (omniStart split)");

    bool cdfOk = (n > 0) && std::fabs(out[(n - 1) * S + 12] - 1.0f) < 1e-6f;
    for (int i = 1; i < n; i++) if (out[i * S + 12] < out[(i - 1) * S + 12]) cdfOk = false;
    check(cdfOk, "merged cdf (slot 12) is monotone and ends at exactly 1");

    // Cap below the total: n is capped and the STATIC set survives intact — overflow
    // only ever drops sprites, never a static wall/flat light.
    std::vector<float> out2(staticN * S, -1.0f);
    int n2 = nee_merge_emitters(sRec.data(), sWgt.data(), staticN,
                                dRec.data(), dWgt.data(), dynN,
                                staticN, out2.data());   // cap = staticN
    bool capOk = (n2 == staticN);
    for (int i = 0; i < staticN; i++) if (out2[i * S + 0] != 100.0f + (float)i) capOk = false;
    check(capOk, "cap drops excess sprites, keeps every static emitter");

    std::printf("  [merge] n=%d (3 static + 2 sprite), capped n=%d (sprites dropped)\n", n, n2);
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

    std::printf("- static|sprite emitter split (DOOM-0084)\n");
    check_merge_ordering();

    return check_summary("nee_sampling");
}
