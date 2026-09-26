// DOOM-0440 — the bloom bright pass's soft-knee weight, in ONE place.
//
// Hand-written, not exported from the Vestige Formula Workbench. The Workbench's own
// bloom_threshold is a different, rational curve; this is DOOM-0331 4.2's quadratic knee.
//
// Two consumers: bloom_extract_raster.comp and bloom_extract_rt.comp. Both chains
// threshold in scene-radiance units against the same kBloomPresets entry (DOOM-0345
// decision 5), so the weight must be the same function on both. It was written out twice,
// and a second copy is how two chains drift apart.
//
// peak is max(r, g, b), NOT Rec.709 luminance. Luminance weights green at 0.7152 and red at
// 0.2126, so a red fireball at four times white would score 0.85 and not bloom while a
// white wall sat on the threshold. peak is also what pbrNeutralToneMapping keys its own
// compression on, so threshold and operator knee end up measured in the same units.
//
// Returns the unitless factor the caller multiplies its colour by: 0 below
// threshold - knee, ramping quadratically through the knee so a surface crossing the
// threshold fades in rather than pops, then the linear excess (peak - threshold) / peak.
//
#ifndef BLOOM_KNEE_GLSL
#define BLOOM_KNEE_GLSL

float bloomKneeWeight(float peak, float threshold, float knee)
{
    float soft = clamp(peak - threshold + knee, 0.0, 2.0 * knee);
    soft       = soft * soft / (4.0 * knee + 1e-4);
    return max(soft, peak - threshold) / max(peak, 1e-4);
}

#endif // BLOOM_KNEE_GLSL
