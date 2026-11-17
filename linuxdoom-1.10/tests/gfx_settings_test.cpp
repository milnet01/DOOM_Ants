// gfx_settings_test.cpp — DOOM-0617: the graphics preset and the ray-tracing
// settings (docs/specs/DOOM-0617-rt-effect-switches.md § 4).
//
// The menu, the `~` key and the config all reach these decisions through
// gfx_settings.h, which needs no GPU, so they are held here. B1 covers the
// preset table, the Ray Tracing row and INV-6's settings half; B5 adds the
// hardware test's decision.
#include "../gfx_settings.h"
#include "check_util.h"

static gfx_settings_t Settings(int mode, int shadows, int ao, int quality, int scale)
{
    gfx_settings_t s = { mode, shadows, ao, quality, scale };
    return s;
}

int main()
{
    // ---- Each preset, applied, reads back as itself ----
    for (int p = GFX_LOW ; p <= GFX_ULTRA ; p++)
    {
        gfx_settings_t s = Settings(RTM_OFF, 0, 0, RTQ_QUARTER, 100);
        GfxApplyPreset(&s, p, 1);
        check_eq_int(GfxMatchPreset(&s, 1), p, gfxPresetNames[p]);
    }

    // ---- Spec § 4.4's table ----
    {
        gfx_settings_t s = Settings(RTM_FULL, 0, 0, RTQ_QUARTER, 100);
        GfxApplyPreset(&s, GFX_HIGH, 1);
        check(s.rtMode == RTM_EFFECTS && s.rtShadows == 1 && s.rtAo == 1
              && s.rtQuality == RTQ_HALF && s.renderScale == 50,
              "High is Effects, both traced effects on, half resolution, scale 50");

        s = Settings(RTM_FULL, 0, 0, RTQ_QUARTER, 100);
        GfxApplyPreset(&s, GFX_MEDIUM, 1);
        check(s.rtMode == RTM_OFF && s.renderScale == 75, "Medium is Off at scale 75");
        check(s.rtShadows == 0 && s.rtAo == 0 && s.rtQuality == RTQ_QUARTER,
              "a preset that does not use Effects leaves its rows as they were");

        GfxApplyPreset(&s, GFX_ULTRA, 1);
        check(s.rtMode == RTM_FULL && s.renderScale == 50, "Ultra is Full at scale 50");
    }

    // ---- Custom ----
    {
        gfx_settings_t s = Settings(RTM_EFFECTS, 1, 1, RTQ_HALF, 50);
        check_eq_int(GfxMatchPreset(&s, 1), GFX_HIGH, "High's rows match High");
        s.rtAo = 0;
        check_eq_int(GfxMatchPreset(&s, 1), GFX_CUSTOM, "one Effects row changed is Custom");
        s = Settings(RTM_OFF, 1, 1, RTQ_HALF, 67);
        check_eq_int(GfxMatchPreset(&s, 1), GFX_CUSTOM, "a render scale no preset uses is Custom");
        s = Settings(RTM_OFF, 0, 0, RTQ_FULL, 50);
        check_eq_int(GfxMatchPreset(&s, 1), GFX_LOW,
                     "the Effects rows do not matter outside Effects");
    }

    // ---- The Preset row's cycle ----
    check_eq_int(GfxNextPreset(GFX_LOW, 1), GFX_MEDIUM, "Low -> Medium");
    check_eq_int(GfxNextPreset(GFX_HIGH, 1), GFX_ULTRA, "High -> Ultra");
    check_eq_int(GfxNextPreset(GFX_ULTRA, 1), GFX_LOW, "Ultra wraps to Low");
    check_eq_int(GfxNextPreset(GFX_CUSTOM, 1), GFX_LOW, "Custom starts again at Low");

    // ---- The Ray Tracing row and the `~` key ----
    check_eq_int(GfxNextRtMode(RTM_OFF, 1), RTM_EFFECTS, "Off -> Effects");
    check_eq_int(GfxNextRtMode(RTM_EFFECTS, 1), RTM_FULL, "Effects -> Full");
    check_eq_int(GfxNextRtMode(RTM_FULL, 1), RTM_OFF, "Full -> Off");
    check_eq_int(GfxToggleRtMode(RTM_FULL, RTM_FULL, 1), RTM_OFF, "~ from Full is Off");
    check_eq_int(GfxToggleRtMode(RTM_OFF, RTM_FULL, 1), RTM_FULL,
                 "~ from Off returns to the last mode, Full");
    check_eq_int(GfxToggleRtMode(RTM_OFF, RTM_EFFECTS, 1), RTM_EFFECTS,
                 "~ from Off returns to the last mode, Effects");
    check_eq_int(GfxToggleRtMode(RTM_OFF, RTM_OFF, 1), RTM_EFFECTS,
                 "~ from Off with no last mode is Effects");

    // ---- Which frame each mode draws ----
    check_eq_int(GfxRtView(RTM_OFF), 0, "Off draws the rasterised frame");
    check_eq_int(GfxRtView(RTM_EFFECTS), 0, "Effects draws the rasterised frame");
    check_eq_int(GfxRtView(RTM_FULL), 6, "Full draws the traced frame");

    // ---- INV-6: a GPU without ray tracing never reaches a traced frame ----
    for (int p = GFX_LOW ; p <= GFX_CUSTOM ; p++)
    {
        gfx_settings_t s = Settings(RTM_OFF, 1, 1, RTQ_HALF, 50);
        GfxApplyPreset(&s, p, 0);
        check_eq_int(GfxRtView(s.rtMode), 0, "no preset traces without ray tracing");
        check(GfxNextPreset(p, 0) <= GFX_MEDIUM, "the Preset row stays at Low and Medium");
    }
    for (int m = RTM_OFF ; m <= RTM_FULL ; m++)
    {
        int preset = GFX_LOW;
        gfx_settings_t s = Settings(m, 1, 1, RTQ_HALF, 50);
        GfxClampSettings(&preset, &s, 0);
        check_eq_int(s.rtMode, RTM_OFF, "rt_mode is held at Off without ray tracing");
        check_eq_int(GfxNextRtMode(m, 0), RTM_OFF, "the Ray Tracing row stays Off");
        check_eq_int(GfxToggleRtMode(m, RTM_FULL, 0), RTM_OFF, "~ stays Off");
    }
    {
        gfx_settings_t s = Settings(RTM_FULL, 1, 1, RTQ_HALF, 50);
        check_eq_int(GfxMatchPreset(&s, 0), GFX_CUSTOM,
                     "without ray tracing, Ultra's rows are not a preset");
    }

    return check_summary("gfx_settings");
}
