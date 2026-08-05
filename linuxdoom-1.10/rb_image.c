#include "rb_image.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG            /* v1 ships PNG heroes/derived only */
/* NOTE: deliberately NOT defining STBI_NO_STDIO. stb_image guards its file-path
   loader (stbi_load(path, ...)) with `#ifndef STBI_NO_STDIO` — merely defining
   the macro, even to 0, satisfies that guard and strips the function out. The
   brief's original `#define STBI_NO_STDIO 0` therefore failed to compile
   (`stbi_load` not declared); omitting the define entirely is stb's documented
   way to keep stdio support (the default). */
/* STBI_ONLY_PNG leaves two int-overflow helpers (stbi__mul2shorts_valid /
   stbi__addints_valid) compiled-but-unused, so vendored stb_image.h trips
   -Wunused-function under -Wall. We don't edit vendored code (dependency rule),
   and can't drop STBI_ONLY_PNG without pulling in every decoder — so scope-silence
   just this header's warnings, not the project's. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop

/* DOOM-0202: PNG *writer* (stbi_write_png), used by the -shotverify headless
   screenshot / visual-regression capture in r_vulkan.cpp. Same vendored-stb
   pattern as the loader above (ADR docs/decisions/0002). Implementation lives
   here so the 1.7k-line header is compiled once in this small C TU, not in the
   big r_vulkan.cpp; r_vulkan.cpp just declares stbi_write_png extern "C". */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

/* DOOM-0294: first free dev-shots/shot-NNNN.png, so a shot never silently
   replaces an earlier one. Lives here rather than in either present path because
   both need it and they are in different languages and translation units. */
int rb_devshot_path(char* out, int outsz) {
    int i;
#ifdef _WIN32
    mkdir("dev-shots");			// Windows mkdir takes no mode arg
#else
    mkdir("dev-shots", 0755);
#endif
    for (i = 1; i <= 9999; i++) {
        FILE* f;
        snprintf(out, (size_t)outsz, "dev-shots/shot-%04d.png", i);
        f = fopen(out, "rb");
        if (!f) return 1;
        fclose(f);
    }
    out[0] = '\0';
    return 0;
}

int rb_image_load(const char* path, rb_image_t* out) {
    int w = 0, h = 0, comp = 0;
    unsigned char* p = stbi_load(path, &w, &h, &comp, 4);   /* force RGBA */
    if (!p) return 0;
    out->pixels = p; out->w = w; out->h = h;
    return 1;
}

/* Box-filter downscale so the longest edge is <= max_edge. Averages the source
   texels covered by each destination texel (fractional box, area-weighted-ish via
   nearest integer coverage — good enough for material maps at <=1024). */
void rb_image_downscale_max(rb_image_t* img, int max_edge) {
    int longest = img->w > img->h ? img->w : img->h;
    if (longest <= max_edge || longest == 0) return;
    double s = (double)max_edge / (double)longest;
    int nw = (int)(img->w * s); if (nw < 1) nw = 1;
    int nh = (int)(img->h * s); if (nh < 1) nh = 1;
    unsigned char* dst = (unsigned char*)malloc((size_t)nw * nh * 4);
    if (!dst) return;                       /* OOM: leave img unchanged (never crash) */
    for (int y = 0; y < nh; y++) {
        int sy0 = (int)((double)y     * img->h / nh);
        int sy1 = (int)((double)(y+1) * img->h / nh); if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < nw; x++) {
            int sx0 = (int)((double)x     * img->w / nw);
            int sx1 = (int)((double)(x+1) * img->w / nw); if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned acc[4] = {0,0,0,0}, cnt = 0;
            for (int sy = sy0; sy < sy1 && sy < img->h; sy++)
                for (int sx = sx0; sx < sx1 && sx < img->w; sx++) {
                    const unsigned char* sp = img->pixels + ((size_t)sy * img->w + sx) * 4;
                    for (int c = 0; c < 4; c++) acc[c] += sp[c];
                    cnt++;
                }
            unsigned char* dp = dst + ((size_t)y * nw + x) * 4;
            for (int c = 0; c < 4; c++) dp[c] = (unsigned char)(cnt ? acc[c] / cnt : 0);
        }
    }
    /* The old buffer came from stbi_load (malloc'd by stb's default allocator);
       free it with plain free() here since we're replacing it with our own
       malloc'd buffer. rb_image_free's stbi_image_free() also just calls free()
       under the hood, so freeing either buffer through either path is safe. */
    free(img->pixels);
    img->pixels = dst; img->w = nw; img->h = nh;
}

void rb_image_free(rb_image_t* img) {
    if (img && img->pixels) { stbi_image_free(img->pixels); img->pixels = NULL; }
}
