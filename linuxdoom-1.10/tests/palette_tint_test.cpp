// palette_tint_test.cpp — DOOM-0379: the palette-flash fit in palette_tint.h.
//
// Solid and Ultra show Classic's damage, pickup and radsuit flashes as one blend,
// out = scale * frame + bias, fitted from palette 0 to the palette Classic would
// swap in. This checks the fit recovers a known blend and reproduces every entry
// to within one colour step, and that palette 0 itself draws nothing.
//
// Build/run: `make test`. No WAD or GPU needed.
#include "../palette_tint.h"
#include "check_util.h"

#include <cmath>

static unsigned char base[768];

// A spread of colours across the whole range, like a real palette.
static void MakeBase()
{
    for (int i = 0; i < 256; i++)
    {
        base[3 * i + 0] = (unsigned char)((i * 37) % 256);
        base[3 * i + 1] = (unsigned char)((i * 91 + 13) % 256);
        base[3 * i + 2] = (unsigned char)((i * 53 + 7) % 256);
    }
}

// The way id built the flash palettes: each colour moved a fraction `a` of the
// way toward `col`, rounded to a byte.
static void MakeFlash(const int col[3], double a, unsigned char* out)
{
    for (int i = 0; i < 768; i++)
        out[i] = (unsigned char)std::lround(base[i] * (1.0 - a) + col[i % 3] * a);
}

static void CheckFlash(const char* name, const int col[3], double a)
{
    unsigned char pal[768];
    MakeFlash(col, a, pal);
    rb_tint_t t;
    RB_FitPaletteTint(base, pal, &t);

    char what[160];
    std::snprintf(what, sizeof what, "%s: the fit is active", name);
    check(t.active == 1, what);

    for (int c = 0; c < 3; c++)
    {
        std::snprintf(what, sizeof what, "%s: channel %d scale is 1 - a", name, c);
        check(std::fabs(t.scale[c] - (1.0 - a)) < 0.005, what);
        std::snprintf(what, sizeof what, "%s: channel %d bias is col * a", name, c);
        check(std::fabs(t.bias[c] - col[c] * a / 255.0) < 0.005, what);
    }

    double worst = 0.0;
    for (int i = 0; i < 768; i++)
    {
        const int c = i % 3;
        const double got = (t.scale[c] * base[i] / 255.0 + t.bias[c]) * 255.0;
        worst = std::fmax(worst, std::fabs(got - pal[i]));
    }
    std::snprintf(what, sizeof what, "%s: every entry within one colour step (worst %.2f)",
                  name, worst);
    check(worst <= 1.0, what);
}

int main()
{
    MakeBase();

    // Palette 0 against itself: nothing to draw, and an exact identity.
    rb_tint_t t;
    RB_FitPaletteTint(base, base, &t);
    check(t.active == 0, "palette 0 is not a flash");
    check(t.scale[0] == 1.0f && t.scale[1] == 1.0f && t.scale[2] == 1.0f,
          "palette 0 fits a scale of exactly 1");
    check(t.bias[0] == 0.0f && t.bias[1] == 0.0f && t.bias[2] == 0.0f,
          "palette 0 fits a bias of exactly 0");

    const int red[3]   = { 255, 0, 0 };
    const int gold[3]  = { 215, 186, 69 };
    const int green[3] = { 0, 255, 0 };
    CheckFlash("damage step 1", red, 1.0 / 9.0);
    CheckFlash("damage step 8", red, 8.0 / 9.0);
    CheckFlash("pickup", gold, 0.25);
    CheckFlash("radsuit", green, 0.125);

    return check_summary("palette_tint_test");
}
