#include <cassert>
#include <cstdio>
#include "../rb_image.c"   // single-TU: pulls in the stb impl + wrappers.
                           // (rb_image.h self-guards its decls with extern "C".)

// Fixtures are the committed bring-up PNGs from Task 1 — no fragile on-the-fly
// generation. startan3_ao.png is a solid 180-grey 64x128 field, so a downscale must
// preserve ~180 everywhere: a clean box-average check. Paths are relative to
// linuxdoom-1.10/ (the CWD `make test` runs the binary from).
static const char* AO = "../assets/ultra/heroes/bringup/startan3_ao.png";

int main() {
    rb_image_t img;
    assert(rb_image_load("/does/not/exist.png", &img) == 0);   // failure never crashes

    assert(rb_image_load(AO, &img) == 1);
    assert(img.w == 64 && img.h == 128);
    assert(img.pixels[0] >= 176 && img.pixels[0] <= 184);      // ~180 grey
    assert(img.pixels[3] == 255);                              // RGB source -> opaque alpha

    rb_image_downscale_max(&img, 32);                          // longest edge 128 -> 32 (=> 16x32)
    assert(img.w == 16 && img.h == 32);
    for (int i = 0; i < img.w * img.h; i++) {                  // solid field stays ~180
        assert(img.pixels[i * 4 + 0] >= 176 && img.pixels[i * 4 + 0] <= 184);
        assert(img.pixels[i * 4 + 3] == 255);
    }
    rb_image_free(&img);
    printf("rb_image: all passed\n");
    return 0;
}
