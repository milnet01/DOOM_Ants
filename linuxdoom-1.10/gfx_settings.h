// gfx_settings.h — DOOM-0617: the graphics preset and the ray-tracing settings,
// as pure decisions on plain values.
//
// docs/specs/DOOM-0617-rt-effect-switches.md § 4 owns the design. The decisions
// live here, rather than in m_menu.c and r_backend.c, so
// tests/gfx_settings_test.cpp and tests/config_bounds_test.cpp can hold them
// with no config file, no menu and no GPU -- the same reason the *_bounds.h
// headers exist.
#ifndef GFX_SETTINGS_H
#define GFX_SETTINGS_H

#include "config_bounds.h"

// The Preset row. Custom is never chosen: it is what the row shows when the
// settings match no preset.
enum { GFX_LOW, GFX_MEDIUM, GFX_HIGH, GFX_ULTRA, GFX_CUSTOM };

// The Ray Tracing row (config key rt_mode).
enum { RTM_OFF, RTM_EFFECTS, RTM_FULL };

// The Trace Quality row: the resolution the Effects passes trace at.
enum { RTQ_QUARTER, RTQ_HALF, RTQ_FULL };

// What the menu and the log call each value, indexed by the enums above.
static const char* const gfxPresetNames[]  = { "Low", "Medium", "High", "Ultra", "Custom" };
static const char* const gfxRtModeNames[]  = { "Off", "Effects", "Full" };
static const char* const gfxQualityNames[] = { "Low", "Medium", "High" };

typedef struct
{
    int rtMode;        // RTM_*
    int rtShadows;     // 0, 1
    int rtAo;          // 0, 1
    int rtQuality;     // RTQ_*
    int renderScale;   // percent: 100, 75, 67 or 50
} gfx_settings_t;

// Spec § 4.4. The traced rows matter only in Effects; a preset with another
// rt_mode leaves them as they are.
static const gfx_settings_t gfxPresets[GFX_CUSTOM] =
{
    { RTM_OFF,     1, 1, RTQ_HALF, 50 },   // Low
    { RTM_OFF,     1, 1, RTQ_HALF, 75 },   // Medium
    { RTM_EFFECTS, 1, 1, RTQ_HALF, 50 },   // High
    { RTM_FULL,    1, 1, RTQ_HALF, 50 },   // Ultra
};

// The highest preset a GPU may use: High and Ultra trace (INV-6).
static inline int GfxTopPreset (int rtCapable)
{
    return rtCapable ? GFX_ULTRA : GFX_MEDIUM;
}

// Bring a hand-edited config into range (INV-9). Each out-of-range value takes
// its key's default. Without ray tracing, rt_mode is held at Off (INV-6).
static inline void GfxClampSettings (int* preset, gfx_settings_t* s, int rtCapable)
{
    *preset      = ConfigClamp (*preset,      GFX_LOW, GFX_CUSTOM, GFX_LOW);
    s->rtMode    = ConfigClamp (s->rtMode,    RTM_OFF, RTM_FULL, RTM_OFF);
    s->rtShadows = ConfigClamp (s->rtShadows, 0, 1, 1);
    s->rtAo      = ConfigClamp (s->rtAo,      0, 1, 1);
    s->rtQuality = ConfigClamp (s->rtQuality, RTQ_QUARTER, RTQ_FULL, RTQ_HALF);
    if (!rtCapable)
	s->rtMode = RTM_OFF;
}

// Set the rows a preset sets. Out-of-range or above the GPU's top: no change.
static inline void GfxApplyPreset (gfx_settings_t* s, int preset, int rtCapable)
{
    const gfx_settings_t* p;

    if (preset < GFX_LOW || preset > GfxTopPreset (rtCapable))
	return;
    p = &gfxPresets[preset];
    s->rtMode      = p->rtMode;
    s->renderScale = p->renderScale;
    if (p->rtMode == RTM_EFFECTS)
    {
	s->rtShadows = p->rtShadows;
	s->rtAo      = p->rtAo;
	s->rtQuality = p->rtQuality;
    }
}

// The preset the rows match, lowest first, or Custom when none does.
static inline int GfxMatchPreset (const gfx_settings_t* s, int rtCapable)
{
    int i;

    for (i = GFX_LOW ; i <= GfxTopPreset (rtCapable) ; i++)
    {
	const gfx_settings_t* p = &gfxPresets[i];

	if (s->rtMode != p->rtMode || s->renderScale != p->renderScale)
	    continue;
	if (p->rtMode == RTM_EFFECTS
	    && (s->rtShadows != p->rtShadows || s->rtAo != p->rtAo
		|| s->rtQuality != p->rtQuality))
	    continue;
	return i;
    }
    return GFX_CUSTOM;
}

// The Preset row's next value: up through the GPU's presets, wrapping to Low.
// From Custom it starts again at Low.
static inline int GfxNextPreset (int current, int rtCapable)
{
    if (current < GFX_LOW || current >= GfxTopPreset (rtCapable))
	return GFX_LOW;
    return current + 1;
}

// The Ray Tracing row's next value: Off, Effects, Full, Off.
static inline int GfxNextRtMode (int rtMode, int rtCapable)
{
    if (!rtCapable || rtMode >= RTM_FULL || rtMode < RTM_OFF)
	return RTM_OFF;
    return rtMode + 1;
}

// The `~` key: Off flips to the last mode that was not Off (Effects when there
// was none); anything else flips to Off.
static inline int GfxToggleRtMode (int rtMode, int lastOn, int rtCapable)
{
    if (!rtCapable || rtMode != RTM_OFF)
	return RTM_OFF;
    return (lastOn == RTM_EFFECTS || lastOn == RTM_FULL) ? lastOn : RTM_EFFECTS;
}

// The path-tracer view (rb_rtdebug) an rt_mode draws. Effects draws the
// rasterised frame and adds its traced passes to it (spec § 4.3).
static inline int GfxRtView (int rtMode)
{
    return rtMode == RTM_FULL ? 6 : 0;
}

#endif // GFX_SETTINGS_H
