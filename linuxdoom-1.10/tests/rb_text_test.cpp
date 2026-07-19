#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "../rb_text.c"   // single-TU: pulls in the stb impl + wrappers.
                          // (rb_text.h self-guards its decls with extern "C".)

// No font ships in the repo yet (Oxanium arrives in DOOM-0206 Task 5), so the bake
// logic is exercised against a real on-disk TTF rather than a fragile embedded blob:
// DejaVu Sans ships with fonts-config on virtually every Linux desktop/CI image. If
// it's ever absent (a minimal container), the test degrades to a skip instead of
// failing the build over a missing system font -- see rb_text_test's own header
// comment / DOOM-0206 Task 1 brief for why this is the chosen approach.
static const char* SYSTEM_TTF = "/usr/share/fonts/truetype/DejaVuSans.ttf";

static unsigned char* read_file(const char* path, long* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(buf); return NULL; }
    *out_len = len;
    return buf;
}

int main() {
    rb_atlas_font_t bad;
    assert(rb_text_bake(NULL, 0, 48, &bad) == 0);   // null input never crashes

    long len = 0;
    unsigned char* ttf = read_file(SYSTEM_TTF, &len);
    if (!ttf) {
        printf("rb_text: skipped (no system TTF at %s)\n", SYSTEM_TTF);
        return 0;
    }

    rb_atlas_font_t font;
    assert(rb_text_bake(ttf, (int)len, 48, &font) == 1);
    assert(font.w > 0 && font.h > 0);
    assert(font.px_height == 48);
    for (int i = 0; i < 96; i++) assert(font.glyphs[i].x1 >= font.glyphs[i].x0);

    float measured = rb_text_measure(&font, "AB");
    float expect = font.glyphs['A' - 32].xadvance + font.glyphs['B' - 32].xadvance;
    assert(measured >= expect - 0.5f && measured <= expect + 0.5f);

    rb_text_free_font(&font);
    free(ttf);
    printf("rb_text: all passed\n");
    return 0;
}
