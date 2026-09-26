// rb_image_test.cpp — PNG load + box-average downscale (DOOM-0042).
//
// Fixture is a committed PNG in tests/fixtures/ — no fragile on-the-fly generation.
// rb_image_solid.png is a solid 180-grey 64x128 field, so a downscale must preserve ~180
// everywhere: a clean box-average check. make_bringup_hero.py first wrote it; it moved here
// (DOOM-0452) so the DOOM-0042 plan's removal of the bring-up set cannot break this test.
//
// The path is anchored to DOOM_TESTS_ROOT (the absolute path of linuxdoom-1.10/, passed by
// the Makefile) rather than being CWD-relative. It used to be a bare "../assets/..." literal,
// which resolved only when the binary was run from linuxdoom-1.10/ — true under `make test`,
// but running the binary directly from anywhere else failed inside the image loader and
// reported as a mysterious test failure rather than a wrong working directory.
#include "check_util.h"

#include <cstdio>
#include "../rb_image.c"   // single-TU: pulls in the stb impl + wrappers.
                           // (rb_image.h self-guards its decls with extern "C".)

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."     /* fallback == the old CWD-relative behaviour */
#endif
static const char* SOLID = DOOM_TESTS_ROOT "/tests/fixtures/rb_image_solid.png";

int main()
{
    rb_image_t img;

    int missing = rb_image_load("/does/not/exist.png", &img);
    check(missing == 0, "loading a missing file fails cleanly (never crashes)");

    int loaded = rb_image_load(SOLID, &img);
    check(loaded == 1, "the solid-grey fixture PNG loads");
    if (loaded != 1)
    {
        std::printf("  (fixture path was %s)\n", SOLID);
        return check_summary("rb_image");
    }

    check(img.w == 64 && img.h == 128, "fixture decodes at its authored 64x128 size");
    check(img.pixels[0] >= 176 && img.pixels[0] <= 184, "first texel is the authored ~180 grey");
    check(img.pixels[3] == 255, "an RGB source decodes to opaque alpha");

    int iw = 0, ih = 0;
    check(rb_image_info(SOLID, &iw, &ih) == 1 && iw == 64 && ih == 128,
          "rb_image_info reads the 64x128 size from the header without decoding");
    int fw = 0, fh = 0;
    rb_image_fit_max(64, 128, 32, &fw, &fh);
    check(fw == 16 && fh == 32, "rb_image_fit_max predicts the downscaled 16x32");

    check(rb_image_downscale_max(&img, 32) == RB_DS_DONE,   // longest edge 128 -> 32
          "a downscale that happens reports RB_DS_DONE, so the caller can log it");
    check(img.w == 16 && img.h == 32, "downscale to a 32px longest edge preserves aspect (16x32)");
    check(rb_image_downscale_max(&img, 32) == RB_DS_NONE,
          "an image that already fits reports RB_DS_NONE");

    bool greyOk = true, alphaOk = true;
    for (int i = 0; i < img.w * img.h; i++)
    {
        if (img.pixels[i * 4 + 0] < 176 || img.pixels[i * 4 + 0] > 184) greyOk = false;
        if (img.pixels[i * 4 + 3] != 255) alphaOk = false;
    }
    check(greyOk, "box-average downscale keeps a solid field at ~180 everywhere");
    check(alphaOk, "downscale keeps every texel opaque");

    rb_image_free(&img);
    return check_summary("rb_image");
}
