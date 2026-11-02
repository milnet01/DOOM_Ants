// gamma_curve.h — DOOM-0491: the gamma setting's curve for the 3D tiers.
//
// Classic maps each palette colour through gammatable[level]. The 3D tiers' picture is full
// colour, so gamma.frag applies pow(colour, exponent) to it instead. These exponents are the
// best fit to each row of gammatable; tests/gamma_curve_test.cpp holds them to it.
#ifndef GAMMA_CURVE_H
#define GAMMA_CURVE_H

#define RB_GAMMA_LEVELS 5

// The exponent for a gamma level, or 0 when the level records no pass: level 0 is the
// identity, and an out-of-range level is treated the same.
static inline float RB_GammaExponent(int level)
{
    static const float kExponent[RB_GAMMA_LEVELS] = { 0.0f, 0.855f, 0.717f, 0.571f, 0.418f };
    return (level >= 0 && level < RB_GAMMA_LEVELS) ? kExponent[level] : 0.0f;
}

#endif
