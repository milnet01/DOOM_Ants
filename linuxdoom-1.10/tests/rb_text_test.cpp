// rb_text_test.cpp — glyph-atlas bake + text measurement (DOOM-0206).
//
// Bakes the BUNDLED Oxanium-SemiBold (assets/Oxanium-SemiBold.ttf, OFL, embedded
// as a C byte array by the Makefile's xxd rule -- the same array r_vulkan.cpp
// bakes at init), so the test exercises the real shipping font and needs nothing
// installed on the host.
//
// It used to hunt for a system DejaVu at three hardcoded distro paths and, when it
// found none, print "skipped" and return 0 -- a GREEN result in which none of the
// bake, glyph-bounds or measurement assertions below ever ran. CI papered over that
// by apt-installing fonts-dejavu-core; any other machine got a vacuous pass. The
// 2026-07-26 test audit called that out (DOOM-0243); baking the bundled font makes
// a missing font impossible rather than silent, and drops the CI font dependency.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD or GPU needed.
#include "check_util.h"

#include <cstdio>
#include "../rb_text.c"                     // single-TU: pulls in the stb impl + wrappers.
                                            // (rb_text.h self-guards its decls with extern "C".)
#include "../assets/Oxanium-SemiBold.ttf.h" // generated: oxanium_ttf[] / oxanium_ttf_len

int main()
{
    // Null input must fail cleanly rather than crash.
    rb_atlas_font_t bad;
    int badrc = rb_text_bake(NULL, 0, 48, &bad);
    check(badrc == 0, "bake with a NULL font buffer fails cleanly");

    // --- the real bake, at the menu's working size ---------------------------------
    rb_atlas_font_t font;
    int rc = rb_text_bake(oxanium_ttf, (int)oxanium_ttf_len, 48, &font);
    check(rc == 1, "bundled Oxanium bakes at 48px");
    if (rc != 1)
        return check_summary("rb_text");   // nothing below is meaningful without a font

    check(font.w > 0 && font.h > 0, "atlas has a non-zero size");
    check(font.px_height == 48, "baked px_height is the requested size");

    bool boundsOk = true;
    for (int i = 0; i < 96; i++)
        if (font.glyphs[i].x1 < font.glyphs[i].x0 || font.glyphs[i].y1 < font.glyphs[i].y0)
            boundsOk = false;
    check(boundsOk, "every glyph rect is well-formed (x1>=x0, y1>=y0)");

    // Vertical metrics drive menu baseline placement. Untested before the audit: a
    // sign or scale error here misplaces every line of text and only shows up by eye.
    check(font.ascent > 0, "ascent is above the baseline (positive)");
    check(font.descent < 0, "descent is below the baseline (negative)");
    check(font.ascent - font.descent > 0, "ascent-to-descent is a positive line height");
    check(font.line_gap >= 0, "line gap is non-negative");

    // rb_text_measure sums the same xadvance floats in the same order, so this is
    // exact -- no tolerance. A tolerance here would mask an off-by-one glyph index
    // whose neighbour happens to be a similar width.
    float measured = rb_text_measure(&font, "AB");
    float expect   = font.glyphs['A' - 32].xadvance + font.glyphs['B' - 32].xadvance;
    check(measured == expect, "measure(\"AB\") is exactly the sum of both advances");
    check(rb_text_measure(NULL, "AB") == 0.0f, "measure with a NULL font returns 0");
    check(rb_text_measure(&font, NULL) == 0.0f, "measure with a NULL string returns 0");

    std::printf("  [48px]  atlas=%dx%d ascent=%d descent=%d line_gap=%d\n",
                font.w, font.h, font.ascent, font.descent, font.line_gap);
    rb_text_free_font(&font);
    check(font.pixels == NULL, "freeing the font clears its pixel pointer");

    // --- the atlas-doubling retry ---------------------------------------------------
    // 96 glyphs at 48px fit the initial RB_ATLAS_START (512) square. At 96px they do
    // not, so the bake must double to 1024 and retry rather than fail. That retry loop
    // had no coverage before the audit.
    rb_atlas_font_t big;
    int bigrc = rb_text_bake(oxanium_ttf, (int)oxanium_ttf_len, 96, &big);
    check(bigrc == 1, "a size that overflows the first atlas still bakes");
    if (bigrc == 1)
    {
        check(big.w > RB_ATLAS_START, "atlas doubled past RB_ATLAS_START to fit 96px glyphs");
        std::printf("  [96px]  atlas=%dx%d (started at %d)\n", big.w, big.h, RB_ATLAS_START);
        rb_text_free_font(&big);
    }

    return check_summary("rb_text");
}
