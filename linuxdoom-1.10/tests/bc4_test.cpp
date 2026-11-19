// bc4_test.cpp — DOOM-0632: the baker's BC4 encoder packs a single-channel block faithfully.
//
// Why this exists: every roughness, metal, occlusion and height map in ultra.hdpack goes
// through RB_Bc4Encode. A wrong bit order or endpoint form would ship as subtly wrong art
// that no load-time check can see.
//
// Contract:
//   1. A flat block, a two-value block, and a block of only 0 and 255 decode exactly.
//   2. A wide ramp decodes within half a palette step and a gentle one within 3, and a ramp with exact 0s
//      keeps them exact (the second endpoint form).
//   3. Each pixel's choice lands at that pixel: a block with one odd pixel decodes with the
//      odd value at the same place, for every place.
//   4. Over pseudo-random blocks the encoder's error never exceeds the plain min-max fit's.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include "../rb_bc4.h"
#include "check_util.h"

namespace {

int worst(const uint8_t a[16], const uint8_t b[16])
{
    int w = 0;
    for (int i = 0; i < 16; i++) w = std::max(w, std::abs((int)a[i] - (int)b[i]));
    return w;
}

void roundTrip(const uint8_t px[16], uint8_t out[16])
{
    uint8_t block[8];
    RB_Bc4Encode(px, block);
    RB_Bc4Decode(block, out);
}

}  // namespace

int main()
{
    uint8_t px[16], got[16];

    for (int v : { 0, 1, 127, 254, 255 })
    {
        for (int i = 0; i < 16; i++) px[i] = (uint8_t)v;
        roundTrip(px, got);
        check(worst(px, got) == 0, "a flat block decodes exactly");
    }

    for (int i = 0; i < 16; i++) px[i] = (i % 3) ? 40 : 200;
    roundTrip(px, got);
    check(worst(px, got) == 0, "a two-value block decodes exactly");

    for (int i = 0; i < 16; i++) px[i] = (i & 1) ? 0 : 255;
    roundTrip(px, got);
    check(worst(px, got) == 0, "a block of only 0 and 255 decodes exactly");

    for (int i = 0; i < 16; i++) px[i] = (uint8_t)(60 + i * 8);   // 60..180
    roundTrip(px, got);
    check(worst(px, got) <= 9, "a ramp decodes within half a palette step");

    for (int i = 0; i < 16; i++) px[i] = (uint8_t)(100 + i);      // 100..115
    roundTrip(px, got);
    check(worst(px, got) <= 3, "a gentle ramp decodes within 3");

    for (int i = 0; i < 16; i++) px[i] = i < 4 ? 0 : (uint8_t)(120 + i * 2);
    roundTrip(px, got);
    {
        bool zeros = true;
        for (int i = 0; i < 4; i++) zeros = zeros && got[i] == 0;
        check(zeros, "exact zeros beside a ramp stay exact");
        check(worst(px, got) <= 3, "the ramp beside exact zeros decodes within 3");
    }

    for (int at = 0; at < 16; at++)
    {
        for (int i = 0; i < 16; i++) px[i] = i == at ? 220 : 30;
        roundTrip(px, got);
        check(worst(px, got) == 0, "a single odd pixel decodes at its own place");
    }

    uint32_t seed = 12345;
    bool neverWorse = true;
    for (int n = 0; n < 2000; n++)
    {
        int lo = 255, hi = 0;
        for (int i = 0; i < 16; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            px[i] = (uint8_t)(seed >> 24);
            if (n & 1) px[i] = (uint8_t)(90 + px[i] / 8);   // every other block is low contrast
            lo = std::min(lo, (int)px[i]);
            hi = std::max(hi, (int)px[i]);
        }
        uint8_t block[8];
        RB_Bc4Encode(px, block);
        const long mine = RB_Bc4Fit(px, block[0], block[1], nullptr);
        const long plain = hi > lo ? RB_Bc4Fit(px, hi, lo, nullptr) : 0;
        neverWorse = neverWorse && mine <= plain;
    }
    check(neverWorse, "the search never does worse than the plain min-max fit");

    return check_summary("bc4_test");
}
