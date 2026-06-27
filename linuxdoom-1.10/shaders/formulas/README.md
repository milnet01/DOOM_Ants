# shaders/formulas

Tuning math for the DOOM-0009 path tracer, kept out of the shader bodies so the
constants and curves have one authored source (the project's no-magic-constants
rule). Two provenances live here:

## Generated — `formulas.glsl`

The Vestige **Formula Workbench** export of the scalar closed-form curves (BRDF
terms, PDFs, exposure, sRGB encode, …). **Do not hand-edit** — regenerate:

```
# from the Vestige tree (tools/formula_workbench built as build/bin/formula_workbench)
formula_workbench --export-glsl --out <tmp> --tier full
cp <tmp>/formulas.glsl linuxdoom-1.10/shaders/formulas/formulas.glsl
```

The file banner records the Workbench version + library hash. Output is
name-sorted and deterministic, so a regenerate diffs cleanly. The path tracer
`#include`s the combined `formulas.glsl` and `glslc -O` strips the functions a
given shader doesn't call.

Currently consumed: `exposureEv` (EV → linear exposure gain) and `linearToSrgb`
(IEC 61966-2-1 display encode). More of the library (GGX/VNDF, Fresnel-Schlick,
MIS power heuristic, RR survival, …) comes online as later DOOM-0009 steps land.

## Hand-written — `pbr_neutral_tonemap.glsl`

Khronos **PBR Neutral** tone mapper. It is a piecewise, cross-channel `vec3`
operator, which is outside the Workbench's *scalar* curve-fit scope — the
Workbench README's "copy as hand-written GLSL" category (like the VNDF basis
transform). Provenance + the ACES/Reinhard rationale are in the file header.
