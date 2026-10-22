// col_wrap_test.cpp — DOOM-0481: a wall column repeats at its texture's real
// height, and a 128-row column reads exactly the rows id's mask did.
//
// Why this exists: R_DrawColumn read dc_source[(frac>>FRACBITS)&127], so a
// column shorter than 128 rows was read past its end. Measured on the stock
// IWADs before the fix: the start views of E1M2, E2M4 and MAP21 each drew a
// few stray pixels from past a short column, and the other maps' start views
// were unchanged by the fix.
//
// Contract:
//   1. For a 128-row column, R_WrapFrac then R_StepFrac per pixel give the
//      same row as id's (frac>>FRACBITS)&127, for any start and step,
//      negative or past INT_MAX included.
//   2. For a column that is not a power of two (72 rows), every row read is
//      inside the column and is the true row modulo 72.
//   3. r_draw.c draws through these helpers and no longer masks with &127
//      (source scrape), so the arithmetic tested here is the engine's.
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../col_wrap.h"
#include "check_util.h"

namespace {

const int FRACBITS = 16;

std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A small deterministic generator, so a failure reproduces.
uint32_t next(uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return s;
}

} // namespace

int main()
{
    // ---- 1: 128 rows draw as id's mask did ----------------------------------
    {
        const unsigned hf = 128u << FRACBITS;
        uint32_t seed = 481;
        bool same = true;
        for (int c = 0; c < 2000 && same; c++)
        {
            const int32_t mid = (int32_t)next(seed);     // any texturemid
            const uint32_t step = next(seed);            // any iscale, > INT_MAX too
            const int yoff = (int)(next(seed) % 2000) - 1000;
            uint32_t idfrac = (uint32_t)mid + (uint32_t)yoff * step;   // id's 32-bit wrap
            unsigned frac = R_WrapFrac(mid + (int64_t)yoff * step, hf);
            const unsigned st = R_WrapFrac(step, hf);
            for (int px = 0; px < 64; px++)
            {
                if ((frac >> FRACBITS) != ((idfrac >> FRACBITS) & 127))
                    same = false;
                idfrac += step;
                frac = R_StepFrac(frac, st, hf);
            }
        }
        check(same, "1: a 128-row column reads the same rows as id's &127");
    }

    // ---- 2: 72 rows wrap at 72 ----------------------------------------------
    {
        const unsigned hf = 72u << FRACBITS;
        uint32_t seed = 72;
        bool inside = true, modulo = true;
        for (int c = 0; c < 2000; c++)
        {
            const int32_t mid = (int32_t)(next(seed) % (400u << FRACBITS)) - (200 << FRACBITS);
            const uint32_t step = next(seed) % (300u << FRACBITS);
            const int yoff = (int)(next(seed) % 400) - 200;
            int64_t truefrac = mid + (int64_t)yoff * step;
            unsigned frac = R_WrapFrac(truefrac, hf);
            const unsigned st = R_WrapFrac(step, hf);
            for (int px = 0; px < 64; px++)
            {
                const unsigned row = frac >> FRACBITS;
                int64_t want = (truefrac >> FRACBITS) % 72;
                if (want < 0)
                    want += 72;
                if (row >= 72)
                    inside = false;
                if ((int64_t)row != want)
                    modulo = false;
                truefrac += step;
                frac = R_StepFrac(frac, st, hf);
            }
        }
        check(inside, "2: a 72-row column is never read past row 71");
        check(modulo, "2: a 72-row column repeats every 72 rows");
    }

    // ---- 3: the engine draws through these (source scrape) ------------------
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_draw.c");
        check(src.find("R_StepFrac (") != std::string::npos,
              "3: r_draw.c steps down the column with R_StepFrac");
        check(src.find("&127]") == std::string::npos,
              "3: r_draw.c no longer masks a column read with &127");
    }

    return check_summary("col_wrap_test");
}
