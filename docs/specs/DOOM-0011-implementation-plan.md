# DOOM-0011 — Volumetric lighting: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL — use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add single-scattering participating media (god-ray shafts + coloured, low-pooling
fog) to the ray-traced view — a view-ray march in `pathtrace.comp` modes 4 & 6, composited
after the SVGF albedo re-multiply, gated on a new `rb_fog` dial.

**Architecture:** A new `marchFog()` samples density + in-scattered light along the primary
view ray `t ∈ [0, tHit]`, from **sky + static emitters only**, with a new directional "sun"
const for slanted sky shafts. It writes `inscatter.rgb` + scalar `transmittance` to one new
half-res `RGBA16F` target; the composite folds `surface·transmittance + inscatter` in **linear
radiance** before the tonemap, on both the surface path and the sky-passthrough branch. Two
free push-constant lanes (`misc6.z` strength, `misc6.w` hell-haze) carry the only runtime
values; everything else is a compile-time `const`.

**Tech Stack:** GLSL compute (`shaders/pathtrace.comp`, `pt_common.glsl`, `svgf_composite.comp`),
Vulkan C++ back-end (`r_vulkan.cpp`), DOOM C menu/config (`m_menu.c`, `m_misc.c`, `i_video.c`),
the cross-thread view struct (`r_mesh.h`, `r_backend.c`). Build: `make` in `linuxdoom-1.10/`.

**Spec:** `docs/specs/DOOM-0011-volumetric-lighting.md`. Read it in full before starting —
this plan implements it; every `§`/`INV`/`Q` reference points there.

> ## ⚠ STATUS — READ BEFORE EXECUTING ANY TASK
>
> - **L1 and L1b are SHIPPED and user-play-tested** (`84e8b35..e7753b3`, `1345c92`). Their
>   unchecked `- [ ]` boxes below are historical, not work outstanding. **Do not
>   re-implement them.**
> - **`L1c` and `L1d` were written on 2026-07-26** from the spec's §4.3a/§4.3b amendments and
>   §7 acceptance rows, and have since been cold-reviewed through loops 7–13 and compiled.
> - **`L1e` (the floor fog, §4.3c / DOOM-0272) is SHIPPED and SIGNED OFF** (`6e3234b`, outdoor
>   half) — it went *before* L1c and L1d, because its outdoor half stands alone while its indoor
>   half waits on L1d's seep. The letters are identifiers, not a sequence.
> - **`L1d` (the seep) is SHIPPED and its Step 7 is SIGNED OFF** (user, 2026-07-27, after
>   DOOM-0276: *"I went through doorways and it all looks just fine and yes the fog does dissipate
>   the further away from an opening to the outside"*) — the fill, the upload and the graded indoor
>   branch are in the tree; E1M1 builds a 75×47 field in 0.6 ms. All three acceptance clauses are
>   now met: the seep **drifts in and thins with depth** (this sign-off), the **sealed room stays
>   clear** (2026-07-27 play-test, INV-12), and the **outdoor look is unchanged** (signed off
>   twice). **Step 6's runtime ≤ 1 % clause was never measured in isolation and is now moot** — the
>   whole fog, tap included, measures **+4.2 %** against a ≤ 15 % gate, so the ≥ 6 % this clause
>   existed to protect for L2–L5 is not in doubt (~11 % is free). **Four deviations from this
>   task's text are recorded in the fix ledger** — read them before writing L1c, since two concern
>   the set-0 plumbing L1c shares.
> - **`L1c` is NOT the next work — a perf pass is.** Its blocker below is now *settled*, and
>   settled badly: Δ was measured on 2026-07-27 at **+34.7 % present-total**, over the whole
>   feature's ≤ 15 % gate, with 95 % of it inside `marchFog` (spec §6). L1c would make that
>   worse — it raises `kFogSteps` 24 → ~40 and doubles the density. **DOOM-0276 ships first**
>   (the open-sky up-ray becomes a channel on L1d's field — spec §4.3a's 2026-07-27
>   amendment, and Step 2 below is superseded by it), then DOOM-0197, then re-measure.
> - **The spec is the authority on every number.** Where this plan and the spec disagree,
>   the spec wins — though no known disagreement remains.
> - **Cold-eyes status: CONVERGED (2026-07-26).** The original design converged in 4 loops; the
>   2026-07-25 amendment took **13**, ending with a pass that returned zero findings. This plan
>   was reviewed as a first-class document from loop 8 on, and **every code block in it has been
>   compiled with `glslangValidator`**. The loop log and the standing regression greps live in
>   `docs/specs/DOOM-0011-fix-ledger.md`; **re-run those greps after any edit** rather than
>   commissioning another loop.
> - ⛔ **The blocker that survived review is now MEASURED, and it failed.** Δ was fog-off vs
>   fog-on over the same session, so it reads as Δ(L1b + L1d + L1e), not Δ(L1b): **+8.38 ms /
>   +34.7 %**. L1c's gate was `8 % − Δ(L1b)`, which is negative. See spec §6's boxed notice
>   for the per-pass split and the caveat on how the two halves were sampled.

## Global Constraints

- **Scope = RT engaged only** (`rb_rtdebug ∈ {4, 6}`). Classic and the raster path (RT off) stay
  **byte-identical by construction** — fog lives only in the modes-4/6 megakernel. No raster or
  Classic file is touched (INV-7).
- **Fog is `rb_fog`-gated**: `rb_fog == 0` skips the whole march (branch not taken) → RT-off-fog
  is byte-identical to today (INV-8). Never compute fog behind a `rb_fog==0` gate.
- **Light sources = sky + static emitters `[0, omniStart)` only** (`omniStart = pc.misc4.y`).
  Dynamic sprite lights `[omniStart, emitCount)`, muzzle (`misc2.z`), flashlight (`misc2.w`)
  **never** scatter (INV-2).
- **Fog is a SEPARATE channel** composited **after** the SVGF albedo re-multiply
  (`svgf_composite.comp:123`) — it never rides `illum`/`gillum`. `inscatter`/`transmittance` are
  **linear radiance**, folded before the tonemap on **both** the surface path **and** the
  sky-passthrough branch (`:93-107`) in the **same** colour space (INV-4).
- **L1 deviation (verified safe, now load-bearing):** `svgf_composite.comp` has a **separate,
  smaller push struct (`SvgfPC`, 120 B) with no `misc6`**, so L1 routed the composite-side fog gate
  through `SvgfPC`'s previously-unused **`misc3.y`**, written C++-side at `r_vulkan.cpp`
  `spc.misc3[1] = pc.misc6[2];` just before the composite dispatch. **Later work touching the fog
  gate inside `svgf_composite.comp` (L5 upsample) uses `misc3.y`, NOT `misc6.z`.** The megakernel
  (`pathtrace.comp`) still uses `misc6.z`/`misc6.w` as specced.
- **No push-struct growth.** Use exactly `misc6.z` + `misc6.w` (the last two free lanes). Do
  **not** append `misc7`; keep `RtPushConstants` at 240 B (`static_assert` `r_vulkan.cpp:7405`,
  `pcr.size` `:2368`) (INV-5).
- **Bake untouched** — `bake.comp` never calls fog; fog is a view-ray term (INV-6).
- **Tuning consts start subtle.** All `kFog*`/tint/strength values below are **starting points**
  labelled *tune-on-hardware*; the look is dialed in with the user on the RX 6600.
- **Modes 4 and 6 stay in lockstep** (as DOOM-0181/0183). A behaviour added to one is added to
  the other in the same task, differing only where §4.6 pins (mode-4 full-res in-megakernel apply;
  mode-6 half-res + composite apply).
- **Always rebuild + smoke after any engine edit** (house rule): `make` then the headless boot
  smoke; report the result. Never leave building to the user.

---

## File structure (what each touched file owns)

| File | Responsibility in this feature | Tasks |
|------|-------------------------------|-------|
| `shaders/pathtrace.comp` | `marchFog()` definition + call site; mode-4 in-megakernel apply; mode-6 half-res write; `wisp()` + the noise tap (**never** in `pt_common.glsl` — INV-6) | L1–L5, incl. L1b–L1d |
| `shaders/pt_common.glsl` | Fog `const`s (steps, density, tints, `kSunDir`, the 2026-07-25 wisp/seep set), phase/density helpers | L1–L5, incl. L1b–L1d |
| `shaders/svgf_composite.comp` | Mode-6 apply: fold fog after albedo re-multiply + on sky-passthrough; **plain bilinear** upsample (L1) → position-guided bilateral (L5) | L1, L5 |
| `r_vulkan.cpp` | New half-res fog image + bindings; the 3-D noise volume + seep field + transform UBO on **set 0** (pool sizes + `PARTIALLY_BOUND`); `rb_fog` extern; `misc6.z/.w` writes; profiler slot | L1, L1c, L1d, L4, L6 |
| `r_mesh.c` | The seep flood fill (portal graph + Dijkstra + per-cell resolve) — lives here because it needs the DOOM map globals; (L1b fallback only) the `RB_MESH_OUTDOOR` bit | L1d, (L1b) |
| `r_mesh.h` | New `rb_view_t.hazeDensity` field; the seep field buffer handle; (L1b fallback only) the `RB_MESH_OUTDOOR` `#define` | L4, L1d, (L1b) |
| `r_backend.c` | Compute hell-haze from `gameepisode`/`gamemap`/sky into `view.hazeDensity` | L4 |
| `m_misc.c` | `rt_fog` config default row | L6 |
| `m_menu.c` | Two menu rows (Effects + Video), `M_ChangeFog`, `fogNames[]` | L6 |
| `i_video.c` | `;` hotkey cycling `rb_fog` | L6 |

## Verification model (read once — applies to every task)

Renderer **look** is a play-test call (per DOOM-0181/0183 and spec §7): L1–L5 acceptance is
**human play-test**, not a unit assertion. The **objective, per-task** cycle is:

1. **Build green** — `make -C linuxdoom-1.10 -j"$(nproc)"` (headers tracked; no `make clean` needed).
2. **Unit tests green** — `make -C linuxdoom-1.10 test`.
3. **Headless boot smoke (no NaN / no crash)** — the DOOM-0203 `-bootsmoke` flag:
   ```bash
   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     ./linuxdoom-1.10/linux/linuxxdoom -iwad wads/doom.wad -warp 1 1 -bootsmoke 105
   # expect: "bootsmoke: 105 tics simulated OK, exiting." (exit 0)
   ```
   (Boot smoke runs the **software** renderer, so it proves the build links + boots; it does
   **not** exercise the RT path. RT look is the screenshot step.)
4. **Play-test screenshot (the look gate)** — launch on the RX 6600, RT engaged, and capture the
   named scene per the harness memory (`SDL_VIDEODRIVER=x11` + the launch/screenshot recipe). The
   per-task "Verify" column in spec §7 is the acceptance description. **This is a user/hardware
   call** — the implementing session captures the screenshot and reports; the user signs off the
   look. Do not mark a layer "done" on build-green alone.

**L6** carries the formal pass/fail gates: `-rtverify` green, `-shotcompare` golden, and the
**≤ 15 % present-total** perf bar (raised from ≤ 5 % by the spec's 2026-07-25 amendment;
§6). Those are real commands, given in Task 6. **L1b, L1c and L1d also carry objective
checks** — L1b a measured ≤ 4 % up-ray Δ, L1c a ≤ 8 % cumulative Δ, L1d a ≤ 20 ms level-load
budget plus a ≤ 1 % runtime tap (spec §7).

**A note on line numbers:** the **symbol name or quoted code is authoritative; the line number
is only a hint.** Locate each insert by the **quoted surrounding code**, never at a raw line
number — earlier tasks shift later lines, and unrelated work shifts them all (DOOM-0254/0263
moved `r_vulkan.cpp` by +4..+6 and `pathtrace.comp` by +2, invalidating every citation in both
these documents at once). **All line numbers here were verified against commit `d925a29`
(2026-07-26).** If HEAD has moved, treat them as advisory and log any drift you fix in
`DOOM-0011-fix-ledger.md`.

---

## Task L1 — March skeleton: sky-ambient glow, half-res target, per-mode apply

**Goal:** A working `marchFog()` that produces a faint uniform air-glow from flat sky ambient
(no direction, no colour, no profiles), composited correctly in both modes. This is the plumbing
spine; every later task fills it in.

**Files:**
- Create: (none — new code lands in existing shaders/back-end)
- Modify: `shaders/pt_common.glsl` (fog consts + helpers)
- Modify: `shaders/pathtrace.comp` (`marchFog()` def; mode-4 apply; mode-6 half-res write)
- Modify: `shaders/svgf_composite.comp` (mode-6 apply after albedo re-multiply + sky-passthrough)
- Modify: `r_vulkan.cpp` (new half-res fog image + its megakernel-write and composite-read bindings)

**Interfaces:**
- Produces: `vec4 marchFog(vec3 ro, vec3 rd, float tHit, FogHit h)` returning
  `(inscatter.rgb, transmittance)` packed `RGBA16F`; the fog image binding indices (chosen here,
  reused by L4/L5/L6). `FogHit` struct = `{ vec3 hitP; vec3 gnormal; uint matFlags; }` — L1 only
  reads none of them yet (sky-ambient is position-independent) but the struct + call signature are
  fixed now so L2–L4 fill fields without re-plumbing.
- Consumes: existing sky sample (`skyPanorama()` `pathtrace.comp:731`, `SKY_COLOR`
  `pt_common.glsl:31`); the mode-6 even/even half-res gate (`pathtrace.comp:1141`).

**Existing code to read first (reuse, do not reinvent):**
- `pt_common.glsl:31` — `SKY_COLOR` const and the block of tuning consts near it (place fog consts
  in the same style).
- `pathtrace.comp:731-733` — `skyPanorama()` + the `misc4.w == 0xFFFFFFFF` no-sky sentinel.
- `pathtrace.comp:915-916` (mode 4) / `:1093-1094` (mode 6) — where `tHit`/`hitP` are resolved
  (the call site is right after this, before the colour write).
- `pathtrace.comp:1023-1024` — the mode-4 `L = 0 on NaN; colour = toneEncode(L);` apply point.
- `pathtrace.comp:1141` — the mode-6 even/even 2×2 half-res gate to mirror.
- `svgf_composite.comp:93-107` — sky-passthrough branch (`if (gp.w < 0.0)`).
- `svgf_composite.comp:123` — `L = albedo * illum + emis * emisMask * ga.a;` and `:133`
  `imageStore(outColor, p, vec4(toneEncode(L), 1.0));`.
- `r_vulkan.cpp` — how an existing storage image (e.g. a denoiser target near `:7330`) is created,
  bound in the descriptor set, and reset; copy that pattern for the fog image.

- [ ] **Step 1: Add the fog consts + helpers to `pt_common.glsl`**

Place near `SKY_COLOR` (`:31`), matching the existing const style. **Values below track the live
`pt_common.glsl`; three were re-tuned on 2026-07-27** (spec §4.3) and the L1 originals are noted
inline. `kFogSkyDist` and `kEyeAboveFloor` arrived later (Q24a) and are not in this listing.

```glsl
// DOOM-0011: volumetric fog (single-scatter view-ray march). All tune-on-hardware.
const int   kFogSteps        = 24;               // fixed sample count (coherent, cheap)
const float kFogMaxDist      = 2048.0;           // clamp tHit so a long corridor can't blow budget
const float kFogBaseDensity  = 0.0033;           // extinction at the layer's base (L1 shipped 0.0008)
const float kFogPoolHeight   = 112.0;            // OUTDOOR e-fold height (L1 48, then 18, then 112)
const float kFogIndoorPool   = 18.0;             // INDOOR e-fold height (added 2026-07-27)
const float kFogAnisotropy   = 0.40;             // Henyey-Greenstein g (mild forward bias); 0 = isotropic
const vec3  kSunDir          = normalize(vec3(0.30, 0.30, 1.0)); // world; +z is up (floor = hitP.z). L2.
const vec3  kGooTint         = vec3(0.35, 0.85, 0.30); // sickly green (L4)
const vec3  kHellTint        = vec3(0.90, 0.35, 0.30); // faint red   (L4)
const float kSkyShaftStrength   = 0.85;          // sky in-scatter gain (L1/L2); L1 shipped 1.0
const float kTorchShaftStrength = 1.0;           // static-emitter in-scatter gain (L3)

// Henyey-Greenstein phase (forward/back scatter weight); cosTheta = dot(viewDir, lightDir).
float fogPhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
}

// L1 shipped this as `fogDensity(vec3 p) { return kFogBaseDensity; }` — a constant. L3's height
// pooling was pulled forward on 2026-07-27, so it now takes the layer's base altitude AND its
// e-fold height, both chosen from the SAMPLE's open-sky test — never from the primary hit
// (see L3 Step 1 for why that distinction is the whole ballgame).
float fogDensity(vec3 p, float baseZ, float poolH) {
    return kFogBaseDensity * exp(-max(0.0, p.z - baseZ) / poolH);
}
```

- [ ] **Step 2: Add the `FogHit` struct + `marchFog()` to `pathtrace.comp`**

Place `marchFog()` above `main()` (near the other traversal helpers). L1 body: fixed-step march,
flat sky ambient in-scatter, dithered start, early-out:

```glsl
struct FogHit { vec3 hitP; vec3 gnormal; uint matFlags; }; // L2-L4 read these

// Returns (inscatter.rgb, transmittance). Linear radiance. rb_fog>0 checked by the caller.
vec4 marchFog(vec3 ro, vec3 rd, float tHit, FogHit h) {
    float tMax = min(tHit, kFogMaxDist);
    float dt   = tMax / float(kFogSteps);
    // Dither the start offset so a fixed step count doesn't band (denoise cleans it, §4.6).
    float jitter = fract(52.9829189 * fract(dot(gl_GlobalInvocationID.xy, vec2(0.06711056, 0.00583715))));
    float t = dt * jitter;

    vec3  inscatter = vec3(0.0);
    float trans     = 1.0;
    vec3  skyAmbient = SKY_COLOR * kSkyShaftStrength;   // L1: flat, non-directional
    float strength   = fogStrengthScale(pc.misc6.z);    // the `;` dial: Low/Med/High thins the air

    for (int i = 0; i < kFogSteps; ++i, t += dt) {
        vec3  p     = ro + rd * t;
        float sigma = fogDensity(p) * strength;
        vec3  Ls    = skyAmbient;                        // L2 adds directional sky + torches
        inscatter += trans * sigma * Ls * dt;
        trans     *= exp(-sigma * dt);
        if (trans < 0.003) break;                        // thick fog occludes the rest cheaply
    }
    return vec4(inscatter, trans);
}
```

**This block is the L1 listing, not the shipped body** — two later steps changed it and are
authoritative over it: **L3 Step 1** replaced the one-argument `fogDensity(p)` with the
per-sample outdoor/indoor base-and-height selection (§4.3 of the spec), and **Q26** replaced the
uniform `t += dt` with the warped march `t = tMax·s²`, `dt = 2·tMax·s/N` (spec §4.3c). Read
`pathtrace.comp:marchFog` before quoting anything here as current.

- [ ] **Step 3: Create the half-res fog image + bindings in `r_vulkan.cpp`**

Mirror an existing storage-image target (copy the creation/binding/reset of a denoiser image near
`:7330`). Add: one `RGBA16F` image at **half render resolution** (`renderExtent/2` rounded up),
a **write** binding for the megakernel descriptor set, and a **read** binding for the
`svgf_composite` descriptor set. Reset/transition it with the other RT targets each frame. Record
the chosen `binding =` indices in a comment (L4/L5/L6 reuse them). Keep it in the RT-only path so
raster is untouched.

- [ ] **Step 4: Call `marchFog()` + apply, mode 4 (in-megakernel, full-res)**

At the mode-4 apply (`pathtrace.comp:1023-1024`), before `colour = toneEncode(L)`:

```glsl
            if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0);
            if (pc.misc6[2] != 0u) {                 // rb_fog strength; 0 = skip entirely (INV-8)
                FogHit fh = FogHit(hitP, gnormal, matFlags);   // gnormal/matFlags: fill per your L1 hit vars (0 ok at L1)
                vec4 fog = marchFog(pc.camPos.xyz, rd, tHit, fh);
                L = L * fog.a + fog.rgb;             // linear radiance, before tonemap (§4.6)
            }
            colour = toneEncode(L);
```

(Use the mode-4 primary-ray dir/`tHit`/`hitP` variables already in scope at `:915-916`.)

- [ ] **Step 5: Write fog to the half-res target, mode 6 (half-res)**

In mode 6, inside the even/even half-res gate (`pathtrace.comp:1141`), after the primary hit
(`:1093-1094`) compute `marchFog()` and `imageStore` the `vec4` into the fog image at the half-res
coord. Guard the whole block with `if (pc.misc6[2] != 0u)`; when `rb_fog==0`, `imageStore` a
neutral `vec4(0,0,0,1)` (zero inscatter, full transmittance) so the composite is a no-op.

- [ ] **Step 6: Apply fog in `svgf_composite.comp`, mode 6 (both branches)**

Fold fog **after** the albedo re-multiply and **in** the sky-passthrough, in linear space. First a
half-res **bilinear** fetch of the fog target (position-guided upsample arrives at L5; L1 uses plain
bilinear).

**Gate on `pc.misc3.y` here, never `misc6.z`.** This shader has its own 120-byte `SvgfPC` push
struct with no `misc6` at all (Global Constraints), so `rb_fog` is mirrored into its free
`misc3.y`; the shipped code reads `pc.misc3.y`. L5 edits this exact block — copying a `misc6`
gate out of here would not compile.

Surface branch — replace `:88-91`:

```glsl
        L = albedo * illum + emis * emisMask * ga.a;   // existing, still linear
    }
    if (pc.misc3.y != 0u) {                             // rb_fog MIRROR; 0 = no fetch, no change (INV-8)
        vec4 fog = fetchFogBilinear(p);                 // half-res -> full-res, plain bilinear (L1)
        L = L * fog.a + fog.rgb;                        // linear, before tonemap (§4.6)
    }
    if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0);
    imageStore(outColor, p, vec4(toneEncode(L), 1.0));
```

Sky-passthrough branch (`:93-107`) — fold fog treating the display-encoded sky as linear, then
re-clamp (§4.6 / Q9 — confirm the round-trip is a no-op when `rb_fog==0`):

```glsl
    if (gp.w < 0.0) {
        vec3 sky = illum;
        if (any(isnan(sky)) || any(isinf(sky))) sky = vec3(0.0);
        if (pc.misc3.y != 0u) {                         // SvgfPC lane, NOT misc6.z
            vec4 fog = fetchFogBilinear(p);
            sky = sky * fog.a + fog.rgb;                // fog in front of visible sky
        }
        imageStore(outColor, p, vec4(clamp(sky, 0.0, 1.0), 1.0));
        imageStore(motion, p, vec4(0.0));
        return;
    }
```

- [ ] **Step 7: Build + smoke + tests**

```bash
make -C linuxdoom-1.10 -j"$(nproc)" && make -C linuxdoom-1.10 test
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./linuxdoom-1.10/linux/linuxxdoom -iwad wads/doom.wad -warp 1 1 -bootsmoke 105
```
Expected: build green (the SPIR-V compile is what catches GLSL errors), tests green,
`bootsmoke: 105 tics simulated OK, exiting.` **Every later task's "Build + smoke + tests"
step means exactly these three commands** — they are not repeated again.

- [ ] **Step 8: Play-test the look (RX 6600, RT engaged)**

Launch RT-engaged (`rb_rtdebug` 4 and 6), any lit room. **Accept (spec §7 L1):** air picks up a
faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; **no NaNs**
(no black/white blowouts); **modes 4 & 6 match**. Capture a screenshot in each mode; report to the
user for look sign-off. (Interim state is expected: **no shafts, no colour** yet — that is L2/L4.)

- [ ] **Step 9: Commit**

```bash
git add linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp \
        linuxdoom-1.10/shaders/svgf_composite.comp linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0011: L1 fog march skeleton — sky-ambient glow, half-res target, per-mode apply"
```

---

## Task L1b — Fog-placement standard (open-sky gate) + sky-backdrop aerial fog

**Goal:** Make the L1 haze obey the **open-sky standard** (spec §4.3a): full haze under open
sky, near-clear under a solid roof, measured **per march sample** by a straight-up shadow ray;
and give the distant **sky backdrop** aerial-perspective fog (spec §4.6a) so the mountains fade
into haze instead of reading crisp. Addresses the user's 2026-07-24 L1 play-test feedback. Ultra/
RT-only and `rb_fog`-gated, like every fog task — see Global Constraints (INV-7/8).

**Files:**
- Modify: `shaders/pt_common.glsl` — add `kIndoorFogScale` const; (fallback only) `RB_MESH_OUTDOOR`.
- Modify: `shaders/pathtrace.comp` — per-sample up-ray in `marchFog`; `skyExposure` into `sigma`;
  sky-backdrop closed-form fog in the mode-4 + mode-6 sky branches.
- Modify (fallback path only, if the perf spot-check demands it): `r_mesh.c` / `r_mesh.h` —
  `RB_MESH_OUTDOOR` bit set at mesh-build.

**Interfaces:**
- Consumes: the shipped `vec4 marchFog(vec3 ro, vec3 rd, float tHit, FogHit h)` (`pathtrace.comp:774-796`),
  its `sigma = fogDensity(p) * strength` line (`:789`), the half-res `fogImg` target (binding 9),
  and the `committed`/`isSky` split (`:849-856`).
- Produces: nothing new for later tasks — L2's sun ray (Task L2) reuses the same shadow-ray
  helper this task first exercises for the up-ray.

**Existing code to read first (reuse, do not reinvent):**
- `pt_common.glsl` fog block (`kFogBaseDensity` etc. near `:36-64`) — add `kIndoorFogScale`
  in the same style; `FLAG_FLAT/FLAG_MASKED/FLAG_EMISSIVE` at `:20-22` (all `const int`).
- The existing **shadow/occlusion ray** helper `occluded()` (`pt_common.glsl:189-195`) and the
  NEE shadow rays in `pathtrace.comp` — the up-ray is the SAME kind of ray-query (cull mask
  `0x01`, `gl_RayFlagsTerminateOnFirstHit`). Mirror its `rayQueryEXT` init; do NOT invent a new one.
- `marchFog` (`pathtrace.comp:774-796`), especially the `sigma` line `:789`.
- Sky handling: `skyPanorama()` (`:735`), the `SKY_FOG_COL` screen-space band (`:761-763`),
  the mode-4 sky write `colour = skyPanorama(...)` (`:1295`), the mode-6 sky branch that sets
  `gpos.w=-1` + stores the sky into `gillum` and `return`s (`~:1283-1292`), and the whole sky
  `else` block (`:1274-1297`).
- Sky-instance mask: the TLAS sky backdrop is instance mask `0x04` (`r_vulkan.cpp:2015`), which
  a `0x01` shadow ray **cannot** hit (`:1918`) — so "open sky" is detected by the up-ray **missing**.
- `fogDensity`/`fogStrengthScale` in `pt_common.glsl`; the mode-4 fog fold + `toneEncode(L)`
  (`pathtrace.comp:1063-1065`); the mode-6 `marchFog` call + `imageStore(fogImg,…)` (`:1189-1195`).

- [ ] **Step 1: Add `kIndoorFogScale` to `pt_common.glsl`**

Next to the other fog consts:

```glsl
const float kIndoorFogScale = 0.05;   // MUST stay > 0 (spec Q12: the `= 0` option is STRUCK --
                                      // L3's torch shafts need a medium in roofed air to light).
                                      // DOOM-0011 §4.3a: fog density multiplier under a solid
                                       // roof (open sky = 1.0). 0.0 = interiors totally clear;
                                       // ~0.05 keeps a faint indoor haze. Tune on hardware (Q12).
```

- [ ] **Step 2: Per-sample open-sky up-ray inside `marchFog` (`pathtrace.comp`)**

> **SHIPPED, THEN REPLACED — 2026-07-27, DOOM-0276.** This step is kept as the record of
> what L1b built and why the *granularity* is per-sample. The up-ray itself is gone: it
> measured as the pole of a +34.7 % fog cost (spec §6), and `openSky` now reads the seep
> field's open-sky mask channel — `texture(uSeepField, worldToSeepUV(p.xy)).g > 0.5` —
> instead of tracing. Spec §4.3a's 2026-07-27 amendment owns the mechanism. **Do not build
> the snippet below into a new tree**; the two branch *values* it sets are still current.

The up-ray is a straight-up (`+Z`) shadow ray with cull mask `0x01`. It cannot hit the mask-`0x04`
sky backdrop, so **MISS = open sky, hit = indoor** (spec §4.3a). Mirror the existing `occluded()`
ray-query init (`pt_common.glsl:189-195`). Because `marchFog` runs in the megakernel it already has
the TLAS in scope. If `occluded()` is directly callable, note its signature is
`occluded(vec3 hitP, vec3 n, vec3 wi, float dist)` — **four** arguments. A fog sample has no
surface, so there is no normal to pass: give the up-vector for both `n` and `wi` (`n` only offsets
the ray origin off a surface to avoid self-intersection, and at a volume sample there is nothing
to self-intersect).
Change the `sigma` line (`:789`) from `float sigma = fogDensity(p) * strength;` to:

```glsl
        // §4.3a open-sky exposure: up-ray misses all solid geometry => under open sky.
        const vec3 up = vec3(0.0, 0.0, 1.0);
        bool  openSky = !occluded(p, up, up, kFogMaxDist);   // 4 args; 0x01 mask, sky is 0x04
        float skyExposure = openSky ? 1.0 : kIndoorFogScale;
        // NOTE (2026-07-26): `skyExposure` gates the SKY-SOURCED term ONLY, never the
        // area profiles -- see spec INV-9 / §4.3b, which owns the single authoritative
        // `sigma_final`. The single-multiplier form below is correct ONLY while L1/L1b
        // have no area profiles; L4 MUST replace it with the split form, or every roofed
        // goo/hell room loses its fog by construction.
        float sigma = fogDensity(p) * strength * skyExposure;   // L1/L1b only -- see note
```

If `occluded()`'s signature differs, mirror it exactly (same cull mask + `TerminateOnFirstHit`);
do not add a new ray-query variant. Keep the up-ray inside the `rb_fog>0` path (the caller already
gates `marchFog`).

- [ ] **Step 3: Sky-backdrop aerial fog — mode 6 (`pathtrace.comp` sky branch)**

The sky backdrop is open-sky by definition, so give it full fog. A **closed form** is exact (no
second march loop). **Shipped shape as of 2026-07-27 — read `skyFogOpticalDepth()` in
`pathtrace.comp`, not this paragraph's history:** `trans = exp(-skyFogOpticalDepth(origin, dir,
strength))`, `inscatter = SKY_COLOR * kSkyShaftStrength * (1 - trans)`. The optical depth is the
exact slant path through the exponential layer, `density-where-you-are × kFogPoolHeight / rd.z`,
clamped at `kFogSkyDist`, so haze varies with the sky pixel's **elevation** — mountains rise out
of the mist. The earlier flat `kFogBaseDensity × strength × kFogSkyDist` form gave every sky pixel
the same haze (spec §4.6a, Q24a).
In the **mode-6 sky branch** (`~:1283-1292`), which currently writes `gpos.w=-1` + the sky into
`gillum` and returns without touching `fogImg`, add — under `if (pc.misc6[2] != 0u)` and the same
even/even half-res gate the surface path uses — an `imageStore(fogImg, ivec2(px)/2, vec4(inscatter, trans))`
so the composite's **existing** fog fold on the sky-passthrough branch
(`svgf_composite.comp:100-103`, unchanged) picks it up. (svgf_composite.comp has no `pt_common`
consts, so the value MUST be computed here — spec §4.6a.)

- [ ] **Step 4: Sky-backdrop aerial fog — mode 4 (`pathtrace.comp` sky branch)**

In the mode-4 sky branch, after `colour = skyPanorama(...)` (`:1328`) and under `if (pc.misc6[2] != 0u)`,
fold the same closed-form fog before the write, in the same linear space as §4.6a:

```glsl
            colour = skyPanorama(px, w, h);
            if (pc.misc6[2] != 0u) {
                float strength = fogStrengthScale(pc.misc6.z);
                float trans    = exp(-skyFogOpticalDepth(origin, dir, strength));
                // skyPanorama returns DISPLAY-encoded colour; in-scatter is LINEAR. Decode,
                // fold in linear like the world path, re-encode (spec Q24b) — folding them
                // directly rendered the same fog ~2.5x darker on the sky than on a wall.
                vec3 skyLin = vec3(srgbToLinear(colour.r), srgbToLinear(colour.g),
                                   srgbToLinear(colour.b));
                colour = toneEncode(skyLin * trans
                                    + SKY_COLOR * kSkyShaftStrength * (1.0 - trans));
            }
```

**DONE 2026-07-27:** the old screen-space `SKY_FOG_COL` band is now switched **off** whenever fog
is on (`if (pc.misc6[2] != 0u) fog = 0.0;` in `skyPanorama`). It is a screen-space wash pinned to
the frame's vertical midpoint, so it can never agree with world fog; L1b's halving only halved the
mismatch, and it was one of the two causes of the reported "hard cut off line". Fog OFF keeps it,
so DOOM-0143's seam protection is intact there. Spec Q14 is CLOSED.

- [ ] **Step 5: Build + smoke + tests** (L1 Step 7 commands).

- [ ] **Step 6: Hardware perf spot-check (RX 6600) — which exposure method ships**

> **SETTLED 2026-07-27, and NOT in the up-ray's favour.** The A/B was finally taken (spec §6's
> boxed notice): fog costs **+8.38 ms / +34.7 %** present-total, 95 % of it inside `marchFog`.
> The Δ this step exists to record is now recorded. The fallback below was **not** the answer
> either — see spec §4.3a's 2026-07-27 amendment (DOOM-0276) for the third path that shipped,
> and §6's lever list for why `RB_MESH_OUTDOOR` must **not** now be applied on top of it.

Per spec §6: with the profiler (`` \ `` **backslash** key — `` ` ``/`~` is the RT view cycle), A/B the **added** present-total (fog-off vs fog-on,
same walk) in the goo room (its ~40 FPS baseline is pre-existing) fits **≤ 4 %** — L1b's slice
of L1c's ≤ 8 % allocation, not the whole-feature gate — AND confirm a **typical non-goo
corridor** scene holds the same ≤ 4 % added share with the up-ray on. (This check originally
read "still holds 60 FPS"; the spec's 2026-07-25 amendment relaxed that floor for RT-engaged
scenes, so the share is the only currency now.) **If it holds** →
the per-sample up-ray ships — and **before closing this step, write the measured Δ into spec §6
and the fix ledger.** L1c's own allowance is `8 % − Δ(L1b)`; leaving the number unrecorded makes
L1c Step 6 unexecutable, which is exactly what has happened so far. **If it misses** → build the cheap fallback instead:
- `r_mesh.h`: `#define RB_MESH_OUTDOOR 0x100` beside the other `RB_MESH_*` bits (`:82-101`).
- `r_mesh.c`: OR the bit into the `flags` word from `seg->frontsector->ceilingpic == skyflatnum`
  (walls — `emit_wall` has `seg`, near `:275`, or at the 4 call sites `529/554/568/582`) and
  `sec->ceilingpic == skyflatnum` (flats — `emit_subsector_caps`, near `:452`).
- `pt_common.glsl:20-22`: `const int RB_MESH_OUTDOOR = 0x100;` — **the same name as the C-side
  `#define`**, matching the neighbouring `FLAG_*` mirrors' one-bit-one-name rule (spec §4.3a:
  "one name, not two").
- In `marchFog`, replace the per-sample up-ray with `float skyExposure = (h.matFlags & uint(RB_MESH_OUTDOOR)) != 0u ? 1.0 : kIndoorFogScale;`
  (whole-view granularity, no doorway cutoff — spec §4.3a fallback).

- [ ] **Step 7: Play-test the look (RX 6600, RT engaged) — user sign-off**

Launch RT-engaged, an open-air map (E1M1 start / the goo courtyard). **Accept (spec §7 L1b):**
open/sky-exposed areas stay hazy; stepping under a roof **clears the air (mist wall at the
threshold** with the up-ray path); distant **mountains fade into haze**, not crisp; sky still
recognizable; `;`-key fog-off is byte-identical. Capture screenshots; report to the user for look
sign-off. (Interim: still **no shafts, no colour** — those are L2/L4.)

- [ ] **Step 8: Commit**

```bash
git add linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp
# (+ r_mesh.c r_mesh.h only if the Step-6 fallback was built)
git commit -m "DOOM-0011: L1b fog-placement standard (open-sky gate) + sky-backdrop aerial fog"
```

---

## Task L1e — The floor fog: a second, short-range density layer (outdoor half)

**Goal:** Mist that pools around the player's feet without turning the far end of the courtyard
white (spec §4.3c, DOOM-0272). One extra density term whose strength falls off with **distance
from the camera** — deliberately not a physical medium, which is exactly why it can be thick at
your feet and absent at 800 units. **Outdoor half only:** the indoor half needs L1d's seep to tell
"room with a window" from "room three doors deep", so it arrives with that task, not this one.
Letters are identifiers, not an order — this ships **before** L1c and L1d.

**Files:**
- Modify: `shaders/pt_common.glsl` — three `const`s + the `floorFogDensity()` helper.
- Modify: `shaders/pathtrace.comp` — one addend in `marchFog`'s `sigma`; one addend in
  `skyFogOpticalDepth`.

**Interfaces:**
- Consumes: `marchFog`'s existing loop variable `t` (the warped sample distance — Q26 shipped,
  `t = tMax·s²`), its per-sample `baseZ`/`skyExposure`, and `skyFogOpticalDepth`'s `h₀`.
- Produces: nothing new — **no descriptor, no push-constant lane, no ray.** INV-5 is untouched, so
  `-rtverify`'s 184-byte prefix cannot move.

**Existing code to read first (reuse, do not reinvent):**
- `fogDensity()` in `pt_common.glsl` — the floor helper is its sibling and must obey the same
  contract: *density at a point is a function of that point alone* (plus, here, `t`, which is a
  property of the ray, not of the world — that asymmetry is the design, §4.3c, and it is the one
  place in this spec where the invariant is knowingly relaxed).
- `marchFog`'s `sigma` line — today `fogDensity(p, baseZ, poolH) * strength * skyExposure`.
- `skyFogOpticalDepth`'s two branches and its `(exp(x) − 1)/x` expansion — the floor addend has the
  same shape and the same near-zero trap, at a *different* threshold.

- [ ] **Step 1: The three `const`s + the helper (`pt_common.glsl`)**

Beside the existing fog block. All three are first guesses; §4.3c carries the arithmetic and Q25
owns them on hardware.

```glsl
const float kFloorFogDensity = 0.010;   // floor-layer extinction AT THE FLOOR (3x the aerial
                                        // layer's kFogBaseDensity there)
const float kFloorFogPool    = 24.0;    // e-fold HEIGHT: knee-deep, so you wade through it
const float kFloorFogRange   = 256.0;   // e-fold DISTANCE FROM THE CAMERA -- the whole trick

// The floor layer. Unlike fogDensity() this takes `t`, the distance along the view ray: the
// term fades with range so mist can be thick underfoot without integrating into a white wall
// at distance. Not physical, and that is the point (spec 4.3c).
float floorFogDensity(vec3 p, float baseZ, float t) {
    return kFloorFogDensity * exp(-max(0.0, p.z - baseZ) / kFloorFogPool)
                            * exp(-t / kFloorFogRange);
}
```

- [ ] **Step 2: The third addend in `marchFog` (`pathtrace.comp`)**

The sum is gated as a whole — INV-9's amended split, `(skySigma + floorSigma) · skyExposure`.
Outdoors `skyExposure` is 1; indoors it is `kIndoorFogScale`, which is what keeps the indoor half
waiting for L1d rather than shipping half-done here.

```glsl
float sigma = (fogDensity(p, baseZ, poolH) + floorFogDensity(p, baseZ, t))
              * strength * skyExposure;
```

- [ ] **Step 3: The second addend in `skyFogOpticalDepth` (`pathtrace.comp`)**

Not optional and not a refinement: leave it out and the skyline gains a ~37 % step against the
walls beneath it (spec §4.3c, INV-10). Both branches, meeting exactly at `rd.z = 0`.

```glsl
    float eH     = exp(-h0 / kFloorFogPool);          // density where the eye is, / D
    float sigmaF = kFloorFogDensity * eH;
    float tauF;
    if (rd.z > 0.0) {
        // Ascending: the exact integral to infinity of the two exponentials' product.
        tauF = sigmaF / (rd.z / kFloorFogPool + 1.0 / kFloorFogRange);
    } else {
        // Descending: falls to the layer base over t1, then a constant floor density below it.
        float dzF = max(-rd.z, 1e-5);
        float t1F = h0 / dzF;
        float aF  = dzF / kFloorFogPool - 1.0 / kFloorFogRange;
        float eT  = exp(-t1F / kFloorFogRange);
        // aF passes through zero at |rd.z| = kFloorFogPool/kFloorFogRange (0.094) -- well
        // inside the visible range, not a corner case -- and the quotient loses all its
        // precision to cancellation there, which is exactly where the two branches must agree.
        float head = (abs(aF) > 1e-6) ? kFloorFogDensity * (eT - eH) / aF
                                      : sigmaF * t1F * (1.0 + 0.5 * aF * t1F);
        tauF = head + kFloorFogDensity * kFloorFogRange * eT;   // + the constant-density tail
    }
```

Then `return strength * (<the existing aerial expression> + tauF);` — one `strength`, applied
once to the sum, in **both** branches of the existing function.

- [ ] **Step 4: Build, verify, play-test, commit**

`make` (the shader compile **is** the identifier check — every symbol above must resolve against
the real tree), then `make test`, then `-rtverify` (must stay green; nothing here touches the push
block). Look acceptance, in the E1M1 courtyard:

- mist pools at your feet and thins as you look away — the far wall is **no whiter than before**;
- **no line along the skyline** where sky meets a distant wall (Step 3's whole purpose);
- indoors is unchanged from L1b — a roofed room shows the same faint haze, not a bank.

```bash
git add linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0011/0272: add the outdoor floor fog (L1e)"
```

---

## Task L1c — The Silent Hill 2 look: near-white fog + two-octave drifting wisps

> ⛔ **BLOCKED until L1b's Δ is recorded.** This task's perf gate is `8 % − Δ(L1b)`, and Δ(L1b)
> has never been measured — spec §6's budget table carries the formula, not a number. Until it
> is written into §6, **L1c Step 6 has no threshold to test against and this task cannot be
> closed.** Take the measurement first (L1b Step 6, on the RX 6600); it is a ten-minute A/B, and
> every loop that has skipped it has left this note standing.

**Goal:** Turn L1b's flat blue-grey haze into the user's 2026-07-25 reference (spec §4.3b): fog
that reads **near-white and colourless**, roughly twice as thick, and full of **billows of
visibly varying thickness drifting slowly past** — with real depth, so a wisp passes in front of
*and* behind a pillar as the camera turns. RT-only and `rb_fog`-gated, like every fog task —
see Global Constraints (INV-7/8).

**Files:**
- Modify: `shaders/pt_common.glsl` — the 2026-07-25 `const`s; `kFogSteps` 24 → ~40;
  `kFogBaseDensity` 0.0033 → ~0.0066 (a ×2 on the shipped value — see the caution in Interfaces).
- Modify: `shaders/pathtrace.comp` — the noise sampler declaration, `wisp()`, the `sigma`
  multiply, and the `SKY_COLOR` → `kFogColor` swaps (foreground **and** both sky closed forms).
- Modify: `r_vulkan.cpp` — generate + upload the 3-D noise volume at startup; **the set-0
  descriptor plumbing** (new binding, pool sizes, `PARTIALLY_BOUND`).

**Interfaces:**
- Consumes: L1b's `skyExposure` line, `misc6.x` (DOOM-0183 ripple time, `r_vulkan.cpp:7455-7457`)
  as the drift clock, and `g.rtDsLayout` (set 0, `r_vulkan.cpp:2335`).
- Produces: **the set-0 plumbing L1d also needs.** Whichever of L1c/L1d lands first pays for the
  pool sizes + `PARTIALLY_BOUND` once; the second only adds its own bindings. Do not build it twice.

**Existing code to read first (reuse, do not reinvent):**
- The fog `const` block in `pt_common.glsl` (`:37-47`) — add the new ones in the same style.
- `marchFog` in `pathtrace.comp`, especially the `sigma` line L1b left as
  `fogDensity(p) * strength * skyExposure`.
- The two sky closed forms that in-scatter `SKY_COLOR` (`pathtrace.comp:1320` and `:1335`) and the
  `SKY_FOG_COL` screen-space band (`:763-764`, mixed at `:771`) that L1b halved.
- `g.rtDsLayout`'s three fixed bindings (`r_vulkan.cpp:2317-2330`), its pool (`:2346`), the set
  alloc (`:2351-2353`), and the four-set layout array (`:2381`). Also the mode-5 `-rtverify` bind
  site (`:6925`) — it binds **this same set**.
- An existing startup image upload (`UploadAtlas`, called from `RB_Vulkan_BuildLevel:7167`) for the
  create/upload/transition idiom — but note the noise volume is **level-independent**, so it is
  built **once after device creation**, not per level.
- `kGrimeWorldScale` (`pathtrace.comp:111`) — the house precedent for a world-scaled noise lookup.

- [ ] **Step 1: Add the 2026-07-25 `const`s to `pt_common.glsl`**

Beside the existing fog block. Every value is a **starting point**, tuned on hardware (Q21).

```glsl
const vec3  kFogColor    = vec3(0.55, 0.56, 0.56); // near-white, LINEAR radiance (spec Q9)
const float kWispAmp     = 0.6;    // density swings 0.4x..1.6x -- billows, not grain
const float kWispWeight2 = 0.7;    // octave-2 weight: CHOSEN, not SH2-derived (spec §4.3b)
const float kWispFreq1   = 1.0/512.0;          // one texel spans 512 world units
const float kWispFreq2   = 2.5 * kWispFreq1;   // finer octave
const vec3  kWispVel1    = vec3( 8.0, 3.0, 1.0);  // world units/sec, INSIDE the freq scale
const vec3  kWispVel2    = vec3(-3.0, 4.0, 0.3);  // deliberately SLOWER than kWispVel1
const vec3  kWispOffset2 = vec3(17.3, 5.1, 23.7); // decorrelates the octaves at t=0 and p=0
```

And **change** two shipped values in the same block:
- `kFogSteps` **24 → 40**. Structured density is a high-frequency signal along the ray and bands
  at 24 (§4.3b). This is a **hypothesis to confirm by looking** — if 24 reads clean with wisps
  on, revert it and bank the budget.
- **Darkening is already applied — check before repeating it.** The user's play-test of the
  shipped fog (spec §4.3b, 2026-07-26) was "I really like the fog, it can be slightly darker
  though. It is quite bright when outside", and `kSkyShaftStrength` went `1.0` → **`0.85`** on
  2026-07-27 in answer. The two changes below both push brightness back *up*, so if it reads
  bright again, take it out of `kSkyShaftStrength` again or out of `kFogColor`; **not** out of
  `kFogBaseDensity`, which the wisps and the ≈2× raise depend on.
- `kFogBaseDensity` **0.0033 → 0.0066** (the shipped value moved; ×2 is the intent, not the
  literal 0.0016 an earlier draft named). Two cautions:
  **(a)** it is the density at the fog layer's **base altitude**, paired with `kFogPoolHeight`
  = 112 outdoors and `kFogIndoorPool` = 18 in roofed air. Doubling it thickens everything at once
  — the ground bank, distant walls and the sky. To move one without the others, change the e-fold
  height instead: a taller layer thickens sight lines and the horizon, a shorter one concentrates
  the bank at your feet and clears the sky faster with elevation.
  **(b)** Re-tune **with wisps on**, never from the un-wisped value: transmittance is non-linear
  in `σ`, so by Jensen wisped fog reads *thinner* on average at the same base density (§4.3b).

- [ ] **Step 2: Generate + upload the 3-D noise volume, and do the set-0 plumbing**

In `r_vulkan.cpp`, once after the device exists (not per level — the volume is level-independent):

- Fill a `64°` `R8_UNORM` staging buffer with uniform `[0,1]` value noise from a **fixed
  compile-time seed**. **Determinism is load-bearing, not incidental:** `-shotcompare`'s golden
  gate (§6) is only a valid pass/fail if the volume is byte-identical run to run, so a
  time-seeded or address-seeded generator silently turns that gate into noise. Use an explicit
  constant seed and a self-contained PRNG; do **not** call `rand()`.
- Create the image `VK_IMAGE_TYPE_3D`, sampler **trilinear** with `REPEAT` on all three axes,
  upload, transition to `SHADER_READ_ONLY_OPTIMAL`. ~256 KB. **No file enters the tree** — it is
  synthesised, so `docs/standards/assets.md` does not apply.
- **Add it to `g.rtDsLayout` (set 0) as binding 3.** `CreateRtComputePipeline` declares
  `VkDescriptorSetLayoutBinding binds[3]` covering bindings 0–2 (TLAS, output storage image,
  mode-5 verify accumulator), so **3 is the next free slot** — widen the array to `binds[4]` and
  bump `dlci.bindingCount` to match. Type `COMBINED_IMAGE_SAMPLER`. Sets 1 and 3 are
  **not** available: both end in a `VARIABLE_DESCRIPTOR_COUNT` bindless array, which Vulkan
  requires to be the highest binding in its set (§5).
- **Two consequences that must ship in this same step, or it fails at runtime:**
  1. `g.rtDsPool` (`:2346`) is sized only for `ACCELERATION_STRUCTURE` + `STORAGE_IMAGE`. Add a
     `COMBINED_IMAGE_SAMPLER` pool size (and, for L1d, `UNIFORM_BUFFER`) or the set allocation
     fails outright.
  2. Set 0 is **the same set the mode-5 `-rtverify` path binds** (`:6925`). The new binding needs
     `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` (or a dummy descriptor bound on that path) —
     **mirror the working `VkDescriptorSetLayoutBindingFlagsCreateInfo bfci` block in the set-1
     bindless material layout** (`r_vulkan.cpp:~3868`), which already does exactly this — or
     "`-rtverify` is unaffected" (INV-6/INV-7) stops being true the moment the binding is declared.
     **Run `-rtverify` in this step, not at L6** — it is the only cheap proof the plumbing is sound.

- [ ] **Step 3: `wisp()` in `pathtrace.comp` — NOT in `pt_common.glsl`**

`bake.comp` `#include`s `pt_common.glsl` verbatim, so a sampler declared there would force the
bake to declare and bind the noise volume — breaking INV-6. Declare the sampler and the helper in
`pathtrace.comp` only. The `const`s from Step 1 may stay in `pt_common.glsl` (unused consts cost
the bake nothing); the **tap** may not.

```glsl
layout(set = 0, binding = 3) uniform sampler3D uNoiseVol;   // Step 2's volume -- pathtrace.comp ONLY

// DOOM-0011 §4.3b: two octaves of drifting 3-D value noise, mean 1.
// noise(u) fetches at texture coord u/N (N = 64 texels), REPEAT-wrapped, so `u` is in
// LATTICE units: one texel spans 1/kWispFreq1 world units. The velocity sits INSIDE the
// frequency scale -- writing noise(p*f + v*t) instead would drift 512x too fast.
float wisp(vec3 p, float t)
{
    float A = 2.0 * texture(uNoiseVol, kWispFreq1 * (p + kWispVel1 * t) / 64.0).r - 1.0;
    float B = 2.0 * texture(uNoiseVol, (kWispFreq2 * (p + kWispVel2 * t) + kWispOffset2) / 64.0).r - 1.0;
    return 1.0 + kWispAmp * (A + kWispWeight2 * B) / (1.0 + kWispWeight2);
}
```

**Mind the `/ 64.0`.** It is the `u → u/N` step of the sampling convention, and **both** taps
above get it. On octave 2 the **whole** argument goes inside the division — the frequency term
*and* `kWispOffset2` — because §4.3b defines the offset as part of `u`. Divide only the frequency
term and the octave-2 lattice shifts by 64× its intended offset. Get it wrong and you see 512-unit
tiling with 8-unit features: exactly the two failure modes §4.3b exists to avoid.

The one alternative is to fold `1/N` into the frequency consts and drop the divide from both taps.
What must never happen is one tap scaled and the other not.

Then multiply it into the density, reusing `misc6.x` as the clock — **no new push lane** (INV-5):

```glsl
        float t_s   = uintBitsToFloat(pc.misc6[0]);   // DOOM-0183 ripple time, seconds
        float sigma = fogDensity(p) * strength * skyExposure * wisp(p, t_s);
```

- [ ] **Step 4: Move the in-scatter tone to `kFogColor` — all three sites**

Miss any one and you get the sky/wall seam this task's own acceptance criterion exists to catch.

1. The **foreground** in-scatter (L1's `skyAmbient = SKY_COLOR * kSkyShaftStrength`).
2. The **mode-6** sky closed form (`pathtrace.comp:1320`).
3. The **mode-4** sky closed form (`:1335`).

**If the mountains read washed-out at High strength**, add a separate sky-only density constant
**lower `kFogSkyDist`** (shipped 2026-07-27 at 4096) rather than adding a second sky constant —
**this no longer works as a cancellation.** Since 2026-07-27 the sky's path is geometric
(`kFogPoolHeight / rd.z`, spec §4.6a) and `kFogSkyDist` is only the layer's horizontal extent, softly
saturated — it bites within
a couple of degrees of the horizon and nowhere else. So doubling the density **will** move the
mountains and there is no one-constant fix. The honest levers are `kFogPoolHeight` (a shallower
layer clears the sky faster with elevation) or a sky-only density. Do **not** lower
`kFogBaseDensity` again to rescue the sky: that undoes the foreground tuning this task just did,
which is why the fork exists. Logged as **Q24/Q24a**. **Judge it from the screenshot.**

The sky closed forms stay **wisp-free** (INV-10): they remain `skyFogOpticalDepth()` and nothing
else. A closed form requires uniform density along the ray, and billow structure on the mountains
would be sub-pixel anyway.

Also **re-judge the `SKY_FOG_COL` screen-space band** (`:763-764`, mixed at `:771`) against the
new near-white base — it overlaps the real distance fog and L1b only halved it (Q14). If the
horizon now reads double-hazed, cut it further or drop it.

- [ ] **Step 5: Build + smoke + tests** (same three commands as L1 Step 7), then **`-rtverify`**.

- [ ] **Step 6: Measure — the half-res vs full-res decision (spec §7 spot-check)**

This is a **go/no-go on which variant ships**, not the formal gate (that is L6's). Per §6's method:

- Measure **half-res first**. Budget: **cumulative** fog-off → fog-on Δ ≤ **8 %** present-total —
  L1c's own increment is `8 % − Δ(L1b)`, so **read L1b's recorded Δ before starting**. If L1b
  already exceeds 8 %, the split gets re-cut; L1c does not simply fail.
- Promote mode-6 fog to **full-res only if** wisps read soft at half-res AND the promotion still
  fits the 8 %. Note the levers multiply: ×1.67 on steps and ×4 on pixels both multiply the
  per-sample up-ray, which §4.3a calls the march's dominant cost.
- **Record the measured Δ.** L2–L5 share what is left of the 15 % after L1c and L1d's ≤ 1 %.
- If full-res ships, note it — **L5 may then largely dissolve** (no upsample left to harden, §6).

- [ ] **Step 7: Play-test the look (RX 6600, RT engaged) — user sign-off**

**Accept (spec §7 L1c):** fog reads **near-white and colourless**, not blue; **billows of visibly
differing thickness drift slowly past**, sitting correctly **in depth** — passing in front of *and*
behind pillars and monsters as the camera turns; no banding at wisp boundaries; no crawl or strobe
in a slow pan; **distant sky still readable at High strength**; **no visible discontinuity at the
sky/wall seam** (INV-10); and **the mountains haze the same near-white as the foreground**, proving
the Step-4 swap landed on all three sites.

**Plus one by-construction check that does not need eyes (INV-11):** with `kFogColor`,
`kFogBaseDensity` and `kFogSteps` **temporarily held at their L1b values**, `kWispAmp = 0` must
render **byte-identical to L1b**. It follows from the multiplicative form (`1 + 0·x ≡ 1`); if it
does not hold, `wisp()` is being added somewhere rather than multiplied.

- [ ] **Step 8: Commit**

```bash
git add linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp \
        linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0011: L1c Silent Hill 2 fog look -- near-white base + two-octave drifting wisps"
```

---

## Task L1d — Outdoor-proximity seep: the load-time distance field

**Goal:** Stop indoor fog being a flat floor everywhere. Fog should **drift in through a doorway
and thin as you walk deeper** (spec §4.3a amendment), graded by a load-time flood-filled distance
to outdoor air — while a **sealed** room that merely shares a wall with outdoors stays exactly as
it was (INV-12). RT-only and `rb_fog`-gated, like every fog task — see Global Constraints.

**Files:**
- Modify: `linuxdoom-1.10/r_mesh.c` / `r_mesh.h` — the flood fill + the field buffer it produces.
  **This is the right home:** the fill needs DOOM map globals (`segs`, `sectors`, `skyflatnum`,
  `P_LineOpening`, `R_PointInSubsector`) and `r_mesh.c` already walks the BSP and already calls
  `R_PointInSubsector` (`RB_SectorAtPoint`, `:692`); `skyflatnum` is already in scope (`:36`).
  It does **not** currently include `p_local.h` — add it for `P_LineOpening`.
- Modify: `r_vulkan.cpp` — upload the field as an `RG16F` 2-D image (`R16F` until DOOM-0276
  added the mask channel) + a small transform UBO, both
  on set 0; rebuild per level in `RB_Vulkan_BuildLevel` beside `g.levelMesh = RB_BuildLevelMesh()`
  (`:7169`).
- Modify: `shaders/pt_common.glsl` — `kSeepMax`, `kSeepFalloff`, `dMax`.
- Modify: `shaders/pathtrace.comp` — the graded indoor branch of `skyExposure`.

**Interfaces:**
- Consumes: L1b's `skyExposure` line; **L1c's set-0 plumbing** (pool sizes + `PARTIALLY_BOUND`).
  If L1d lands first, build that plumbing here instead — see L1c Step 2, and add the
  `UNIFORM_BUFFER` pool size either way.
- Produces: nothing later tasks consume.

**Existing code to read first:** `RB_SectorAtPoint` (`r_mesh.c:692`); `P_LineOpening`
(`p_maputl.c:300-331`); `RB_Vulkan_BuildLevel` (`r_vulkan.cpp:~7145-7175`).

- [ ] **Step 1: Build the portal graph and run Dijkstra (`r_mesh.c`)**

Read spec §4.3a's numbered steps first — the node choice is the part that is easy to get wrong.

- **Nodes are PORTALS, not sectors and not subsectors.** One node per surviving `seg_t`, sited at
  its midpoint. A sector-indexed search settles **one** distance per sector, which is exactly the
  flat-per-room result Step 2 forbids. A subsector graph fails differently: vanilla DOOM has **no
  minisegs** (`P_LoadSegs` gives every seg a linedef, `p_setup.c:196-198`), so two BSP leaves of
  the same room share no seg and every multi-leaf hall would come out disconnected.
- A seg **survives** iff it has a `backsector`, its `linedef` is two-sided, **and**
  `P_LineOpening(seg->linedef)` leaves `openrange > 0`. `P_LineOpening` returns **`void`** and
  writes the file-scope globals `opentop`/`openbottom`/`openrange`/`lowfloor` — call it and read
  the global; do not re-derive it. Those globals are **not re-entrant**, so keep the fill
  **single-threaded**. A closed DOOM door is still a two-sided linedef, which is why the opening
  test (not a one-sidedness test) is what stops fog leaking through every shut door.
- **Also require `linedef->frontsector != linedef->backsector`.** A self-referencing sector (the
  vanilla deep-water / fake-wall trick) is two-sided with a full-height opening but is *drawn*
  solid — without this test the flood walks straight through it. This is INV-12's leak in another
  costume, and INV-12 is false without it.
- **Edges join two portals that share a sector**, weighted by the distance between their midpoints.
  **Seed** every portal at `d = 0` if *either* of its sectors has `ceilingpic == skyflatnum`, then
  run Dijkstra from the whole seed set at once. Weights are non-negative and the graph is finite,
  so it terminates.

- [ ] **Step 2: Resolve `d` per GRID CELL and rasterise the field (`r_mesh.c`)**

64-unit cells over the map's XY extent, **padded by one cell of void beyond the bounding box**.

```
d(cell) = min over the portals of the cell's OWN sector of ( d(portal) + |cell centre - portal| )
```

clamped to `dMax`; an outdoor cell is `0`. `R_PointInSubsector` maps a cell centre to its leaf and
thence to `->sector`. **Resolving per node instead would defeat the feature** — `d` would be
constant across a room, so the seep would step at the room boundary and hold flat inside, which is
precisely what the user asked to soften.

**The three degenerate cases, pinned so you do not have to guess:**
- **No open sky anywhere on the level** (most hell maps): the seed set is empty → **every cell gets
  `dMax`** and the seep collapses to exactly `kIndoorFogScale`, i.e. the shipped L1b look. Correct
  behaviour, not a failure.
- **Unreachable cells** (a sealed room; any cell in void space): the **finite** sentinel
  `dMax = 8 · kSeepFalloff`. It must be finite — a half-float `+inf` under a zero bilinear weight
  yields `NaN`, which propagates into `σ` and blows the whole march.
- **XY extent exceeds the budget** (sized for ≤ `256×256` cells): **double the cell size and
  rebuild**, repeating until it fits. Coarser cells only blur the seep's edge; they cannot break
  INV-12, because connectivity was decided on the seg graph before rasterisation.

- [ ] **Step 3: Upload the field + its transform UBO (`r_vulkan.cpp`)**

- **`RG16F`, two channels since DOOM-0276** (`.r` = `d`, `.g` = the open-sky mask; it was
  single-channel `R16F` as originally written) — **not `R8`**: normalising `d` against `kSeepFalloff` would cap
  representable distance at 192 units, flooring `exp(-d/kSeepFalloff)` at `e⁻¹ = 0.368` and so
  `skyExposure` at ≈`0.22`, four times the intended indoor floor, everywhere.
- **Sampler state is part of the contract: `CLAMP_TO_EDGE` on both axes.** Under `REPEAT` an
  outdoor `d = 0` at one edge would wrap onto indoor air at the opposite edge. The Step-2 padding
  ring is what makes `CLAMP_TO_EDGE` extend `dMax` outward rather than an outdoor `0`.
  **Corrected 2026-07-27 (DOOM-0276):** this bullet used to justify itself with "a march sample
  can legitimately land outside the map's XY box (the `tHit` clamp lets `p` run toward the sky
  backdrop)", and **that is not true**. `marchFog` is called only on the surface-hit branch
  (`pathtrace.comp:1255`, `:1384`); a sky pixel takes §4.6a's closed form and never marches. Every
  sample therefore lies on the segment between the camera and a real geometry hit, both of which
  are inside the box. Keep `CLAMP_TO_EDGE` — it is still the right sampler and costs nothing — but
  do not reason from the false premise: it is what made a reviewer conclude the padding ring was
  load-bearing at runtime, when the real edge defect was the grid's truncating extent (fixed with
  `ceilf` in the same change).
- **A small UBO carries the world→texel transform** (map XY origin, inverse cell size, texel
  dimensions). This is **per-level runtime data**, so it can be neither a compile-time `const` nor
  a push lane (INV-5 is full). Without it the shader cannot turn `p.xy` into a texture coordinate.
  INV-5 is about the `RtPushConstants` block and is unaffected by a UBO in a descriptor set.
- Rebuild both per level in `RB_Vulkan_BuildLevel`, beside `g.levelMesh = RB_BuildLevelMesh()`.

**Fix the shader-side declarations here, so both sides agree.** Set 0, bindings **4** (field) and
**5** (UBO), continuing from L1c's binding 3 — widen `binds[]` and `dlci.bindingCount` again, and
add a `UNIFORM_BUFFER` pool size beside L1c's `COMBINED_IMAGE_SAMPLER` one:

```glsl
layout(set = 0, binding = 4) uniform sampler2D uSeepField;   // RG16F, CLAMP_TO_EDGE both axes
                                                            // .r = seep distance (L1d)
                                                            // .g = open-sky mask  (DOOM-0276)
layout(set = 0, binding = 5) uniform SeepXform {
    vec2 origin;    // world XY of texel (0,0)'s CENTRE -- inside the padding ring, see below
    vec2 invCell;   // 1.0 / cell size per axis (world units -> cells)
    vec2 dims;      // texel dimensions as float, for the UV divide
} seep;

vec2 worldToSeepUV(vec2 worldXY)
{
    return ((worldXY - seep.origin) * seep.invCell + 0.5) / seep.dims;
}
```

C++ side: the same three `vec2`s in the same order (`float origin[2]; float invCell[2];
float dims[2];` — `std140`-safe, 24 B, no padding surprises at this size).

**`origin` is the PADDED grid's texel-0 centre, not the map's minimum corner.** Step 2 padded the
grid by one cell of void beyond the XY bounding box, so texel (0,0) sits one full cell *outside*
the map. Feed it the un-padded corner and every lookup is off by one cell, which quietly breaks the
padding ring INV-12 depends on — the failure looks like fog leaking at map edges, not like a bad
transform.

- [ ] **Step 4: Grade the indoor branch (`pt_common.glsl` + `pathtrace.comp`)**

```glsl
const float kSeepMax     = 0.5;    // density multiplier right at an open doorway
const float kSeepFalloff = 192.0;  // world units; e-folding distance inward
const float dMax         = 8.0 * kSeepFalloff;  // finite unreachable/void sentinel
```

Then replace L1b's flat indoor floor — **one bilinear tap, no rays** (INV-12):

```glsl
        // §4.3a amendment: the open-sky branch is still exactly 1.0; only indoor changes.
        // DOOM-0276 folded the open-sky TEST into this same tap -- `.g` is the mask.
        vec2  seep    = texture(uSeepField, worldToSeepUV(p.xy)).rg;
        bool  openSky = seep.g > 0.5;
        float skyExposure = openSky ? 1.0
                                    : mix(kIndoorFogScale, kSeepMax, exp(-seep.r / kSeepFalloff));
```

**`kIndoorFogScale` must stay > 0** (Q12's `= 0` is struck): L3's torch shafts need a medium in
roofed air to light. And this gates the **sky-sourced** term only — never `areaMult` (INV-9); L4's
`σ` split is what keeps that true.

- [ ] **Step 5: Build + smoke + tests** (L1 Step 7 commands), then **`-rtverify`**.

- [ ] **Step 6: Measure — both budgets (spec §7 L1d)**

Two separate numbers; neither substitutes for the other.
- **Level load ≤ 20 ms on E1M1.** Time the flood fill directly — it runs once, beside the mesh
  build. If it misses, coarsen the cell size before optimising the search.
- **Runtime ≤ 1 % present-total** on the §6 walk. INV-12's "single bilinear tap" is **per march
  sample**, inside the loop §6 calls the dominant cost — it is *not* free merely because the fill
  is load-time. This 1 % comes out of the ≥ 7 % left after L1c, leaving ≥ 6 % for L2–L5.

- [ ] **Step 7: Play-test the look (RX 6600, RT engaged) — user sign-off**

**Accept (spec §7 L1d):** standing in a doorway onto a courtyard, **a little fog drifts in and
thins as you walk deeper**; a **sealed** room that merely shares a wall with outdoors is
**visually indistinguishable from the same room before L1d** — it shows the plain
`kIndoorFogScale` floor and no seep, which is what proves the fill is through-open-space and not
straight-line (INV-12); and the **outdoor look is unchanged from L1c**, since the seep touches
only the indoor branch.

**Pick the sealed-room test scene deliberately** — it is the only acceptance item that can falsify
INV-12, so it needs a room you have confirmed *is* sealed and *does* share a wall with outdoor air,
not whichever closet is nearest.

- [ ] **Step 8: Commit**

```bash
git add linuxdoom-1.10/r_mesh.c linuxdoom-1.10/r_mesh.h linuxdoom-1.10/r_vulkan.cpp \
        linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0011: L1d outdoor-proximity fog seep via a load-time distance field"
```

---

## Task L2 — Sky shafts: directional sun + per-sample sky-visibility ray + HG phase

**Goal:** Turn the flat glow into **slanted beams**. Add the sun direction, cast one shadow ray
toward it per sample, and weight the in-scatter by the HG phase so shafts read as beams.

**Files:**
- Modify: `shaders/pathtrace.comp` (`marchFog()` inner loop: sun-visibility ray + phase)
- Modify: `shaders/pt_common.glsl` (only if a phase/helper tweak is needed — consts already exist)

**Interfaces:**
- Consumes: `kSunDir`, `fogPhaseHG`, `kFogAnisotropy` (L1); the existing sky-visibility test
  (`pathtrace.comp:870-871`, the `isSky` test — but see the note in the code sketch: L2's own
  sky detection is the ray MISS, not a custom-index hit) + whatever shadow/any-hit ray-query helper the
  primary trace uses.
- Produces: directional `Ls` in `marchFog()` (torch sources add to it at L3).

**Existing code to read first:**
- `pathtrace.comp:870-871` — the primary-ray `isSky` test (custom-index 2). Read it for context,
  but do **not** reuse it as L2's predicate
  to decide "did the sun ray reach sky?".
- The primary trace's ray-query setup in `pathtrace.comp` — reuse its `rayQueryEXT` /
  `traceRayEXT` pattern for the one sun ray; **confirm the helper signature there**, do not invent.
- `pathtrace.comp:731-733` — the `misc4.w == 0xFFFFFFFF` no-sky sentinel (skip the sun ray, no sky).

- [ ] **Step 1: Replace L1's flat `skyAmbient` with a per-sample directional sky term**

In `marchFog()`'s loop, cast one ray from `p` toward `kSunDir`; if it reaches a sky instance, add
the sky radiance weighted by the phase; else the sample is dark (the bright/dark boundary *is* the
shaft). Guard the whole sky term on "level has sky" so enclosed levels skip it (§4.4(a)):

```glsl
        bool skyExists = (pc.misc4[3] != 0xFFFFFFFFu);    // the no-sky sentinel; declare it, it is new
        vec3 Ls = vec3(0.0);
        if (skyExists) {
            // Sky is detected by the ray MISSING all solid geometry, NOT by a
            // custom-index-2 hit: the shadow cull mask 0x01 cannot hit the sky-backdrop
            // instance (mask 0x04, r_vulkan.cpp:2020), so a committed sky hit is
            // impossible on this ray. Spec §4.4(a) / cold-eyes loop 2.
            if (sunRayMissesGeometry(p, kSunDir)) {      // one ray; mirrors the L1b up-ray
                float ph = fogPhaseHG(dot(rd, kSunDir), kFogAnisotropy);
                Ls += kFogColor * kSkyShaftStrength * ph;   // spec §4.3b: kFogColor, NOT SKY_COLOR
                // and this REPLACES L1's flat `skyAmbient` term -- it does not add to it,
                // or open air in-scatters sky light twice (spec §4.4(a)).
            }
        }
        // L3 adds torch contributions to Ls here.
```
**Delete L1's now-dead pre-loop line.** L1 computes `vec3 skyAmbient = SKY_COLOR *
kSkyShaftStrength;` once *above* the loop and seeds `Ls` from it. The block above replaces that
seed, so `skyAmbient` has no reader left — remove the declaration in this same edit rather than
leaving a dead local.

> **Dependency — `kFogColor` is declared by Task L1c Step 1**, which the spec's build order (§7)
> puts before L2. Run L1c first and the constant exists. If L2 is attempted out of order, fall
> back to `SKY_COLOR` here and swap it when L1c lands — do not leave the symbol undefined.

Define `sunRayMissesGeometry()` — the same name the snippet above calls — next to `marchFog()`
using the confirmed ray-query helper; it traces from `p + kSunDir*eps` along `kSunDir` and returns
true iff the ray **misses all solid geometry**. It never tests for a sky *hit*: cull mask `0x01`
cannot reach the mask-`0x04` sky instance, so "reaches sky" *is* "missed everything"
(spec §4.4(a)).

- **Also fix the stale comment L1 shipped.** `pathtrace.comp:808` reads
  `// L2 adds directional sky + torches`, but L2 **replaces** the flat `skyAmbient` — only L3
  adds. Leaving it invites exactly the double-count §4.4(a) forbids.

- [ ] **Step 2: Build + smoke + tests** (same three commands as L1 Step 7).

- [ ] **Step 3: Play-test (spec §7 L2)**

**Accept:** a doorway/sky-hole open to sky throws a **visible slanted beam**; closed rooms stay
clear; the beam **moves correctly** as the camera orbits (parallax against the geometry).
E1M1's opening (sky over the zigzag) is a good scene. Watch Q8 — the beam must read strong without
clipping to a flat white slab under the PBR-Neutral tonemap. Screenshot modes 4 & 6; user sign-off.

- [ ] **Step 4: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0011: L2 sky shafts — kSunDir + per-sample sky-visibility ray + HG phase"
```

---

## Task L3 — Height pooling + torch shafts (static emitters, nearest-few, no occlusion)

**Goal:** Fog **settles low** into a floor layer, and **torches glow their surrounding air** in
dark rooms — using only the existing static emitter slice.

**Files:**
- Modify: `shaders/pt_common.glsl` (`fogDensity()` gains height pooling)
- Modify: `shaders/pathtrace.comp` (`marchFog()`: torch loop over `[0, omniStart)`)

**Interfaces:**
- Consumes: `FogHit.hitP`/`FogHit.gnormal` (now read), `kFogPoolHeight`; the `Emitters` buffer
  (14 floats/record, `pt_common.glsl:52-56`) and `omniStart = pc.misc4.y` (`r_vulkan.cpp:7400`);
  `kTorchShaftStrength`.
- Produces: final density shape + torch `Ls`; L4 multiplies these by the profile tint.

**Existing code to read first:**
- `pt_common.glsl:52-56` — emitter record layout (`v0 v1 v2 Le cdf pdf`); how the centroid + `Le`
  are read elsewhere (find an existing emitter read to copy the field offsets).
- `r_vulkan.cpp:7395-7405` — `omniStart = pc.misc4[1]` write + the DOOM-0084 static/dynamic
  boundary comment (confirms `[0, omniStart)` is static).

- [x] **Step 1: Height pooling in `fogDensity()`** — **SHIPPED EARLY 2026-07-27**, ahead of L2,
  and then corrected twice the same day. Read the shipped `marchFog()`; the draft below is history.
  **What it took three passes to learn, spelled out in spec §4.3:** the drafted design derived the
  cloud's ground height from the PRIMARY HIT (`hitP.z` when it faced up, else the camera's floor).
  That is wrong, and wrong in a way review cannot see. Standing above a courtyard it puts two
  clouds at two heights in one frame — walls hazed, the ground in front of them clear.
  **The invariant: density at a point must be a function of that point alone, never of what the
  ray carrying it eventually hits.** Shipped instead, chosen per SAMPLE from its open-sky test:
  open sky → `pc.fogFloorZ` (per-level, `rb_mesh_t::fogFloorZ` = lowest open-sky floor) with
  `kFogPoolHeight` = 112; roofed → `ro.z - kEyeAboveFloor` with `kFogIndoorPool` = 18. Both are
  per-frame constants, so the invariant holds. `kFogFloorFallback` was never added.
  **`fogHeightPool()` was NOT split out** — the shipped `fogDensity()` inlines the `exp()`. L4
  Step 3 calls `fogHeightPool(...)`, so **L4 must extract it first**; it will not compile otherwise.

The superseded draft that stood here — one per-pixel floor reference derived from the primary
hit, plus a `kFogFloorFallback` const — has been **deleted rather than kept as history**, because
it is code an implementer could copy and it is the exact defect described above. The ledger's
batch 15 holds the record. What shipped:

```glsl
// pt_common.glsl
float fogDensity(vec3 p, float baseZ, float poolH) {
    return kFogBaseDensity * exp(-max(0.0, p.z - baseZ) / poolH);
}

// pathtrace.comp, inside marchFog()'s per-sample loop -- chosen from THIS SAMPLE's open-sky test
float baseZ = openSky ? uintBitsToFloat(pc.fogFloorZ) : (ro.z - kEyeAboveFloor);
float poolH = openSky ? kFogPoolHeight                : kFogIndoorPool;
float sigma = fogDensity(p, baseZ, poolH) * strength * skyExposure;
```

**Why the pool factor should still be its own function.** L4 stops calling `fogDensity()` and builds `sigma`
from two separate terms, but it still needs the height pooling — and a local inside
`fogDensity()`'s body is not visible to any caller. Splitting it out now is what makes L4's
edit a one-liner; do not inline it back.

- [ ] **Step 2: Torch shafts — nearest-few static emitters, no occlusion (Q2 start cheap)**

In `marchFog()`'s loop, after the sky term, add contributions from the **static** slice only. Do
**not** shadow-test every emitter every sample. Pick the nearest few (by distance to the emitter
centroid) and add each as `Le · falloff(dist) · phase · kTorchShaftStrength`:

```glsl
        // Torch shafts: static emitters [0, omniStart) only (INV-2). Nearest-few, no occlusion (Q2).
        uint omniStart = pc.misc4[1];
        const int kTorchTaps = 4;                         // spec §4.4(b): "the nearest few"
        uint  best[kTorchTaps];  float bestD2[kTorchTaps];
        for (int j = 0; j < kTorchTaps; ++j) { best[j] = 0xFFFFFFFFu; bestD2[j] = 1e30; }

        // Pass 1 -- cheap: one dot product per emitter, keep the nearest few.
        for (uint k = 0u; k < omniStart; ++k) {           // consider only static emitters
            vec3  toL = emitterCentroid(k) - p;           // from the Emitters buffer (:52-56)
            float d2  = dot(toL, toL);
            int worst = 0;
            for (int j = 1; j < kTorchTaps; ++j) if (bestD2[j] > bestD2[worst]) worst = j;
            if (d2 < bestD2[worst]) { bestD2[worst] = d2; best[worst] = k; }
        }

        // Pass 2 -- the expensive part (the phase function), only for the few that survived.
        for (int j = 0; j < kTorchTaps; ++j) {
            if (best[j] == 0xFFFFFFFFu) continue;
            vec3  toL     = emitterCentroid(best[j]) - p;
            float falloff = 1.0 / (1.0 + bestD2[j] * kTorchFalloff);
            float ph      = fogPhaseHG(dot(rd, normalize(toL)), kFogAnisotropy);
            Ls += emitterLe(best[j]) * falloff * ph * kTorchShaftStrength;   // NO occlusion ray in v1
        }
```
Add `const float kTorchFalloff` to `pt_common.glsl`. `emitterCentroid`/`emitterLe` = small helpers
reading the record fields (copy the offsets from the existing emitter read you found).

> **Perf risk — read this before building, it may change the shape of the step (Q23).** The two
> passes above cut the *phase* evaluations from `steps × omniStart` down to `steps × 4`, which is
> the saving the spec's "nearest few" is for. But **pass 1 is still `steps × omniStart` distance
> tests** — at ~40 steps and E1M1's static-emitter count that is thousands of iterations per pixel,
> and it may not fit L3's share of the budget on its own. The named fallback is to run the
> selection **once per ray** (before the march loop, from the ray's midpoint) instead of per
> sample, trading a little accuracy on long rays for an `omniStart`-sized scan per pixel rather
> than per sample. **Measure pass 1 alone first**; if it does not fit, take the per-ray form and
> record the deviation in the spec. Do not silently drop back to evaluating every emitter — that
> is strictly worse and contradicts §4.4(b).

- [ ] **Step 3: Build + smoke + tests** (L1 Step 7 commands).

- [ ] **Step 4: Play-test (spec §7 L3)**

**Accept:** fog **settles low** into a floor layer (not a uniform room-fill); a **torch in a dark
room glows** its surrounding air; **dynamic/muzzle/flashlight do NOT scatter** (fire a fireball,
toggle the flashlight — the air must not strobe). Screenshot modes 4 & 6; user sign-off.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/shaders/pt_common.glsl linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0011: L3 height pooling + torch shafts (static emitters, no occlusion)"
```

---

## Task L4 — Area profiles + colour: goo tint, hell haze, medium tint

**Goal:** Colour the fog by area — **green in goo rooms** (primary-hit liquid flag), **faint red
haze on hell levels** (a new per-level flag crossing the thread seam), with shaft colour =
light × medium tint.

**Files:**
- Modify: `r_mesh.h` (add `rb_view_t.hazeDensity`)
- Modify: `r_backend.c` (compute the hell flag → `view.hazeDensity`)
- Modify: `r_vulkan.cpp` (write `g.lastView.hazeDensity` bit-cast into `misc6.w`)
- Modify: `shaders/pathtrace.comp` (`marchFog()`: pick profile, apply `mediumTint`, add haze)

**Interfaces:**
- Consumes: `MatCtrl.flags` via a **new `FogHit.ctrlFlags` field this task adds** (Step 4 — the
  liquid bit is *not* in `FogHit.matFlags`, which carries per-vertex flags); `LIQUID_NUKAGE = 8u`
  (`pathtrace.comp:472`, mirroring `RB_FLAG_LIQUID_NUKAGE` in `rb_materials.h:17`);
  `kGooTint`, `kHellTint`; `misc6.w` (haze density, bit-cast float).
- Produces: final coloured `inscatter`, plus the widened `FogHit` struct. Nothing later consumes
  new interfaces.

**Existing code to read first:**
- `r_mesh.h:265-273` — `rb_view_t` (`x,y,z,angle,extralight,skytexnum`); add the field here.
- `r_backend.c:181` — `view.skytexnum = skytexture;` — compute the hell flag beside it (this brings
  `gameepisode`/`gamemap` into scope; they are C globals in the doom source).
- `r_vulkan.cpp:7426-7429` — the `misc6` write block (`misc6[1]=rb_wet; misc6[2]=0; misc6[3]=0;`).
  Mirror the `misc6[0]` ripple bit-cast (`std::memcpy(&pc.misc6[0], &rippleSec, sizeof(float))`)
  for `misc6[3]`.
- `rb_materials.h:17` — `RB_FLAG_LIQUID_NUKAGE`; `r_vulkan.cpp:5910` `FlagLiquidFlats` (how the
  flag is set on a flat) — confirms the flag reaches the primary-hit material.

- [ ] **Step 1: Add the `rb_view_t` field**

In `r_mesh.h` (`:265-273`), add after `skytexnum`:
```c
    float hazeDensity;   /* DOOM-0011: hell-level global haze; 0 on non-hell levels */
```

- [ ] **Step 2: Compute the hell flag in `r_backend.c`**

Beside `view.skytexnum = skytexture;` (`:181`), apply the spec's concrete v1 rule (§4.5):
```c
    /* DOOM-0011: hell haze — Inferno (E>=3), DOOM-II hell run (map>=20), or a fire/hell sky. */
    boolean hell = ((gamemode == registered || gamemode == retail) && gameepisode >= 3)
                || (gamemode == commercial && gamemap  >= 20);
    /* Name the two DOOM-1 modes, do NOT write `gamemode != commercial`. GameMode_t is
       { shareware, registered, commercial, retail, indetermined } (doomdef.h), so `!= commercial`
       also admits shareware and the no-IWAD `indetermined` state. It is inert today — shareware
       ships Episode 1 only — but it is not the rule spec §4.5 states, and a wider IWAD would
       diverge silently. */
    /* (Optional: OR in a fire-sky texture test if skytexture names a hell sky.) */
    view.hazeDensity = hell ? kHazeDensityDefault : 0.0f;
```
Add `kHazeDensityDefault` as a small file-scope const in `r_backend.c` (tune-on-hardware; start
subtle). Confirm `gamemode`/`gameepisode`/`gamemap` are declared (they are DOOM globals — include
the header that already declares them if `r_backend.c` doesn't see them).

- [ ] **Step 3: Write `hazeDensity` into `misc6.w`**

In `r_vulkan.cpp`, at the `misc6` block (`:7429`), replace `pc.misc6[3] = 0u;` with the bit-cast
(mirroring the `misc6[0]` ripple pattern):
```cpp
    float haze = g.lastView.hazeDensity;           // DOOM-0011: hell haze -> misc6.w (bit-cast float)
    //   `g.lastView`, NOT a bare `view` -- RecordRtTrace() takes no rb_view_t parameter. Every
    //   other per-frame field in this function reads off g.lastView (g.lastView.angle, .x,
    //   .extralight, a few lines above). A bare `view` does not compile here.
    std::memcpy(&pc.misc6[3], &haze, sizeof(float));
```

- [ ] **Step 4: Apply profiles + tint in `marchFog()`**

**First, widen `FogHit` — the liquid bit is not in `matFlags`.** `FogHit.matFlags` is filled at both
call sites from the per-**vertex** flags word (`FogHit fh = FogHit(hitP, n, uint(flags));`), whose
live bits are `FLAG_FLAT`/`FLAG_MASKED`/`FLAG_EMISSIVE` and friends (`pt_common.glsl:20-22`). The
liquid bit lives somewhere else entirely: `LIQUID_NUKAGE = 8u` is a **`MatCtrl.flags`** bit
(`pathtrace.comp:472`, read via `isNukage(mc)`). Testing `h.matFlags` for it would be testing an
unrelated vertex bit — the goo branch would never run, and L4's own acceptance criterion ("a goo
room fills green") would fail with no compile error to warn you. So:

1. Add a field: `struct FogHit { vec3 hitP; vec3 gnormal; uint matFlags; uint ctrlFlags; };`
2. Fill it at **both** call sites — `FogHit fh = FogHit(hitP, n, uint(flags), mc.flags);`. `mc`
   (`MatCtrl mc = ctrl[id];`) is already in scope at both; grep to confirm before editing.

Then read the profile from the primary hit + haze, set `mediumTint` and a density multiplier, add
the global haze to base density, and multiply every `Ls` contribution by `mediumTint`:

```glsl
    // Profile select (§4.5): default clear; goo if primary hit is liquid nukage; hell haze global.
    vec3  mediumTint = vec3(1.0);
    float areaMult   = 0.0;                                   // spec §4.5: clear = 0, goo = 1.0
    float haze       = uintBitsToFloat(pc.misc6[3]);          // hell haze (0 on non-hell)
    if ((h.ctrlFlags & LIQUID_NUKAGE) != 0u) {                // MatCtrl bit, NOT h.matFlags
        mediumTint = kGooTint;
        areaMult   = 1.0;
    }
    if (haze > 0.0) {
        mediumTint *= kHellTint;                              // tints MULTIPLY (spec §4.5)
    }
```
- **Split the `sigma` line — this is the step that discharges L1b's standing note and spec
  INV-9, and it is easy to miss because the obvious edit (folding into `fogDensity`'s return)
  does NOT do it.** The L1/L1b call site is
  `float sigma = fogDensity(p) * strength * skyExposure;` — everything it returns gets
  multiplied by `skyExposure`. Since goo and hell rooms are **roofed**,
  `skyExposure = kIndoorFogScale ≈ 0.05` there, so folding the profile density into
  `fogDensity` would crush it to ~5 % of intent and every goo/hell interior would lose the
  fog L4 exists to tint. The play-test could still pass *weakly* (a thin green tint), which
  is why this needs a code-level check, not an eyeball. Write the split form instead:

```glsl
        // spec §4.3b sigma_final: skyExposure gates the SKY-SOURCED term ONLY (INV-9).
        // Loop body -- `p` and `skyExposure` are per-sample, so this goes INSIDE marchFog's for
        // loop, replacing the old sigma line. (8-space fencing = loop body, as elsewhere here.)
        float pool      = fogHeightPool(p, baseZ, poolH); // extract it from fogDensity() first --
                                                         // L3 shipped it inlined (see L3 Step 1).
                                                         // baseZ/poolH are marchFog's per-sample
                                                         // open-sky choice, NOT a per-pixel floorZ
        float skySigma  = kFogBaseDensity * skyExposure;  // outdoor haze, gated
        float areaSigma = (kAreaDensity * areaMult) + haze; // profiles, NOT gated
        float sigma     = (skySigma + areaSigma) * pool * wisp(p, t_s) * strength;
    //   `areaMult` comes from the profile-select block above (spec §4.5's per-profile weight).
    //   `baseZ`/`poolH` and `t_s` are all already local to `marchFog()`'s loop -- the first two
    //              from L3 Step 1's per-sample open-sky choice, `t_s` from L1c Step 3. Nothing
    //              new to plumb. There is no per-pixel `floorZ`; do not reintroduce one.
    //   `wisp`     is a FUNCTION (L1c Step 3), so it must be called: `wisp(p, t_s)`. The bare
    //              identifier is not a value and will not compile. If L4 is somehow reached
    //              without L1c, substitute the literal `1.0` -- keeping the factor in place is
    //              what makes L1c a one-line edit later.
```
- **`fogDensity()` loses its last caller here.** Once `sigma` is built from the split terms,
  grep for `fogDensity(` — if nothing else calls it, delete it in this same commit rather than
  leaving a dead helper. `fogHeightPool()` is what the line above calls — extract it out of the
  shipped `fogDensity()`, which currently inlines the same `exp()`.
- **Verify by construction, not by eye:** with `rb_fog` on, a goo room under a solid roof
  must show green fog at a density independent of `kIndoorFogScale` — temporarily setting
  `kIndoorFogScale = 0` must NOT remove the goo pool. If it does, the split did not land.
- Multiply the sky term **and** each torch term by `mediumTint` (colour = light × medium): so
  L2's `Ls += kFogColor * kSkyShaftStrength * ph;` becomes
  `Ls += kFogColor * kSkyShaftStrength * ph * mediumTint;`, and likewise for each torch term.
- Add `const float kAreaDensity` (start `0.0020`) to `pt_common.glsl`. The spec's §5 inventory
  names **`kAreaDensity`** — there is no `kGooDensityMul`.

- [ ] **Step 5: Build + smoke + tests** (L1 Step 7 commands).

- [ ] **Step 6: Play-test (spec §7 L4 — needs two levels)**

**Accept:** a **goo room fills green and pools low**; a **hell level (E3M1) gains a faint red
haze** while **E1M1 does not** (the concrete rule is checkable); a **torch shaft reads
warm-through-green** in goo; clear levels stay neutral. Warp to E1M1 (goo room, no haze) and E3M1
(haze). Screenshot; user sign-off.

- [ ] **Step 7: Commit**

```bash
git add linuxdoom-1.10/r_mesh.h linuxdoom-1.10/r_backend.c linuxdoom-1.10/r_vulkan.cpp \
        linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0011: L4 area profiles + colour — goo tint, hell haze, medium tint"
```

---

## Task L5 — Denoise / quality pass: position-guided upsample, dither + phase tune

**Goal:** Make the fog **smooth, not grainy or crawling** in a slow pan, holding shaft shape. Swap
L1's plain-bilinear upsample for a **position-guided bilateral** one, with the sky-seam bilinear
fallback; tune dither, phase, anisotropy.

**Files:**
- Modify: `shaders/svgf_composite.comp` (plain bilinear → the position-guided `fetchFog`; SHIPPED)
- Modify: `shaders/pathtrace.comp` / `pt_common.glsl` (dither + `kFogAnisotropy` tuning only)

**Interfaces:**
- Consumes: the fog image (L1), the gbuffer **world position** (`gpos`, already read in
  `svgf_composite.comp`), the sky sentinel `gp.w < 0.0` (`:66`).
- Produces: the final upsampled fog fetch used by both composite branches. No new interfaces.

**Existing code to read first:**
- `svgf_composite.comp:93` — the `gp.w < 0.0` sky sentinel; `:53-66` — `fetchFogBilinearPlain`'s
  four-texel load (the bilateral guide + the
  fallback trigger).
- `r_vulkan.cpp:7561-7568` — the a-trous passes (the **escalation** path, Q6, only if bilateral crawls).

- [x] **Step 1: Position-guided bilateral upsample** — **SHIPPED 2026-07-27**, pulled forward because the artifact it fixes was reported from hardware: a fringe about one fog texel wide hugging every silhouette, measured at ~13-16 display pixels on a 50 %-render-scale frame. The shipped `fetchFog()` follows the sketch below, plus the one detail the sketch left as `...`: taps whose own gbuffer texel is sky are skipped, and if all four are skipped it falls back rather than dividing by zero.

**There is no depth buffer to guide this with — use the world position that is already there.**
`gpos` holds the primary hit's **world position** in `gp.xyz`; `gp.w` is the **material id**
(`uint id = uint(gp.w + 0.5);` on the surface branch), with `gp.w < 0.0` as the sky sentinel. A
weight built from `gp.w` would be comparing material ids, which is meaningless. Guiding on
`gp.xyz` needs no new image and no new push-constant lane.

Replace the plain bilinear with a bilateral fetch: for each of the four half-res fog texels around
`p`, read the gbuffer at **that texel's own full-res pixel** (`imageLoad(gpos[pc.misc.x], q * 2)`) and
weight it by how far its hit point sits from the centre pixel's, so a shaft against a near wall
doesn't bleed onto far geometry. **At sky pixels** (`gp.w < 0.0`, §4.6) there is no hit point to
compare → **fall back to plain bilinear** there, keeping the shaft-against-sky reconstruction
smooth:

```glsl
// First rename the shipped `fetchFogBilinear` to `fetchFogBilinearPlain`, body unchanged --
// it becomes the fallback. `fetchFog` below is the new entry point both branches call.
//
// kFogDepthSigma is declared HERE, in svgf_composite.comp -- NOT in pt_common.glsl, which this
// shader does not include (only formulas.glsl + pbr_neutral_tonemap.glsl). Spec 4.6a leans on
// that same limitation to justify computing the sky fog in the megakernel; putting this const
// in pt_common.glsl walks straight into it.
const float kFogDepthSigma = 256.0;   // world (DOOM) units; tune -- see the note below

vec4 fetchFog(ivec2 p, vec4 gpFull) {
    if (gpFull.w < 0.0) return fetchFogBilinearPlain(p);   // sky seam: no guide (§4.6)
    // else: 4-tap bilateral over the half-res fog target,
    //   weight_i = bilinear_i * exp(-length(hit_i - gpFull.xyz) / kFogDepthSigma)
    //   where hit_i = imageLoad(gpos[pc.misc.x], q_i * 2).xyz
    //   `pc.misc.x` -- NOT `cur`. `cur` is a local inside main(); this function sits above main()
    //   like fetchFogBilinear does, so it cannot see it. Do NOT "fix" a compile error here by
    //   hardcoding gpos[0] -- that reads the wrong half of the double-buffered gbuffer.
    // Skip any neighbour that is itself sky (its own gp.w < 0.0). If all four are skipped,
    // return fetchFogBilinearPlain(p) so the fetch always yields something.
    ...
}
```
`kFogDepthSigma` is a **distance in world (DOOM) units**, not a depth ratio: start near a room
width and tune down until fog stops bleeding across a near/far edge. Wire both composite branches
to `fetchFog(p, gp)` — pass the whole `gp`, not `gp.w`.

**DONE:** the shipped comment above the (now renamed) `fetchFogBilinearPlain` said "Depth-guided
(bilateral) upsample arrives at L5 (Q6)", written before the guide moved to world position. It now
reads "Position-guided", changed in the same edit as the rename.

> **Verified by compiler, 2026-07-26.** This step was reconstructed into the real
> `svgf_composite.comp` — rename, the new const, `fetchFog()` written out in full, and **both**
> call sites wired — and compiled with `glslangValidator -V`: clean. The two defects this check
> caught (`gpos[cur]`, and the const in `pt_common.glsl`) are fixed above.

- [ ] **Step 2: Tune dither + phase (look only)**

Adjust the dither (IGN vs the L1 hash) and `kFogAnisotropy` / `kFogSteps` if shafts read busy or
band. Isotropic (`kFogAnisotropy = 0`) is the fallback if HG reads too busy (Q5). Keep changes to
consts.

- [ ] **Step 3: Build + smoke + tests** (L1 Step 7 commands).

- [ ] **Step 4: Play-test (spec §7 L5)**

**Accept:** fog is **smooth**, not grainy or crawling, in a **slow pan**; shafts **hold their
shape** (no swimming edges). Do a slow orbit in the goo room and at a sky shaft. If it **crawls**,
escalate to routing the fog channel through the a-trous passes (`r_vulkan.cpp:7545`, Q6) — note
that decision in the commit. Screenshot; user sign-off.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/shaders/svgf_composite.comp linuxdoom-1.10/shaders/pathtrace.comp \
        linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0011: L5 denoise pass — position-guided fog upsample + dither/phase tune"
```

---

## Task L6 — Runtime dial + menu + hotkey + profiler + perf gate (the objective gate)

**Goal:** Wire the user-facing controls, add a GPU-timer slot for the fog pass, and **pass the
objective gates**: `-rtverify` green, `-shotcompare` golden re-blessed (if on-by-default), and
**≤ 15 % present-total** vs fog-off (spec §6, 2026-07-25). This task carries the *formal*
gate; the earlier layers carry their own measured spot-checks.

**Files:**
- Modify: `r_vulkan.cpp` (`rb_fog` extern; `misc6.z` write; profiler `queryCount`+resets+readback;
  DOOM-0208 canonical-config pin)
- Modify: `m_misc.c` (`rt_fog` config default)
- Modify: `m_menu.c` (two menu rows, `M_ChangeFog`, `fogNames[]`, draw/label/value sites)
- Modify: `i_video.c` (`;` hotkey)

**Interfaces:**
- Consumes: `misc6.z` (already read by the shaders since L1 as `pc.misc6[2]`); the DOOM-0208 arm
  block (`r_vulkan.cpp:8212`).
- Produces: the shipped `rb_fog` dial (0..3) driving all fog cost.

**Existing code to read first:**
- `r_vulkan.cpp:1000` — `extern "C" { int rb_wet = 1; }` (place `rb_fog` beside it).
- `r_vulkan.cpp:7427` — `pc.misc6[1] = rb_wet ? 1u : 0u;` (place the `rb_fog` write beside it).
- `r_vulkan.cpp:8212` — DOOM-0208 canonical-config arm block (`rb_fog` pinned there since 2026-07-26).
- `r_vulkan.cpp:1520` `queryCount = 8` (profiler pool, full — fog takes slot 8, so it becomes
  `9`); `:7330`, `:8326` resets (both hardcode `0, 8`); `:8118` readback;
  `:8076-8086` readback — the four sites to widen for the fog timer slot.
- `m_misc.c:274` — `{"rt_wet", &rb_wet, 1}` config row; `rt_fog` already added at `:275`.
- `m_menu.c` — the `rb_detile` multi-value pattern to clone: `effects_e`/`videoitem_e` enums
  (`:501-510`/`:543-565`), `EffectsMenu[]`/`VideoMenu[]` (`:512-520`/`:567-588`),
  `M_DrawEffectsMenu` `"De-tile:"`+`detileNames[]` row (`:1474-1476`), `videoLabels[]` (`:1500`),
  `M_VideoCrispValue` `case vid_detile:` (`:1567`), `M_ChangeDetile`.
- `i_video.c:412-487` — the toggle-key block (`]`/`[`/`'`/`~`/`` ` ``/`` \ ``); `;` (`SDLK_SEMICOLON`)
  was free and now carries the fog dial. **The GPU profiler is `` \ `` (backslash)** — `` ` ``/`~`
  is the RT view cycle, a different key (spec §6).

- [x] **Step 1: `rb_fog` extern + config + push write** — DONE EARLY in commit `f8c6b1f`
  (pulled forward so L1–L5 are viewable). Remaining here: **only the DOOM-0208 canonical-config
  pin** (`r_vulkan.cpp` arm block ~`:8212`) — add `rb_fog` beside `rb_detile=2, rb_filth=1,
  rb_wet=1`.
  *(original step, done except the pin:)*

- `r_vulkan.cpp` beside `rb_wet` (`:1000`): `extern "C" { int rb_fog = 1; }`
  (subtle "Low" on by default — matches `rb_wet=1`/`rb_filth=1`/`rb_detile=2`; **Q10** — flip to
  `0` if review prefers off-by-default; if `0`, skip the golden re-bless in Step 6).
- `m_misc.c` beside `rt_wet` (`:270`): `{"rt_fog", &rb_fog, 1}` (match the `rb_fog` default).
- `r_vulkan.cpp` beside `misc6[1]` (`:7427`): `pc.misc6[2] = (uint)rb_fog;` (replaces the `= 0u`).
- `r_vulkan.cpp` DOOM-0208 arm block (`:8212`): **DONE** (2026-07-26 debt sweep added
  `rb_fog = 1;` to the pin). What remains owed is the golden **re-bless** with fog on.
  Historical intent: pin `rb_fog` to its shipped default alongside
  `rb_detile=2, rb_filth=1, rb_wet=1`.

- [x] **Step 2: Menu rows (seven edits + name table — clone `rb_detile`, place like `rb_wet`)**

> **This step may land early as DOOM-0266.** That item splits the fog row out of this task,
> because the `;`-key dial is user-facing *today* and defaults to **on**, while `EffectsMenu[]`
> carries six rows and no fog row — the invisible-toggle problem DOOM-0205 exists to prevent.
> If DOOM-0266 ships first, **tick this step rather than adding the row twice**, and check its
> work against the seven-edit list below: leaving one edit short is the failure this step's own
> history records.

**DONE 2026-07-27 (DOOM-0266)** — all seven landed together; the list is kept as the record of
what "add a menu row" actually costs in this codebase. Per spec §5, adding only the menuitem
arrays ships a blank row:
1. `ef_fog` in `effects_e`, `vid_fog` in `videoitem_e`.
2. Row in `EffectsMenu[]` and `VideoMenu[]`, both bound to `M_ChangeFog`.
3. `M_DrawEffectsMenu`: a `"Volumetric fog:"` label + `fogNames[rb_fog]` value keyed on `ef_fog`
   (mirror the `"De-tile:"` row, **not** the boolean `"Wet liquid:"`).
4. `videoLabels[]` entry for `vid_fog`.
5. `M_VideoCrispValue`: `case vid_fog: return fogNames[rb_fog];` (mirror `case vid_detile:`).
6. `M_ChangeFog` mirroring `M_ChangeDetile` (`rb_fog = (rb_fog + 1) % 4;`), plus the name table
   `char fogNames[4][6] = {"Off","Low","Med","High"};` — spec §5 pins the **fixed 2-D form**
   matching `detileNames` (`char detileNames[3][7]`, `m_menu.c:1229`), **not** a `const char*[]`.
7. **Forward declaration** `void M_ChangeFog(int choice);` beside `void M_ChangeWet(int choice);`
   (`m_menu.c:224`). Without it the menuitem arrays reference an undeclared function and the file
   does not compile.

- [x] **Step 3: `;` hotkey** — DONE EARLY in commit `f8c6b1f` (`SDLK_SEMICOLON` cycles
  Off/Low/Med/High in `i_video.c`, mirroring the `]`/`[`/`'` toggles). Nothing left here.

- [ ] **Step 4: Profiler slot for the fog pass**

The number **8 is hardcoded in seven places**, not three. Every one moves together, or the readback
writes past the end of a stack array — a stack overflow that will not announce itself as one:

| Site | Now | Becomes |
|---|---|---|
| `queryCount` (`r_vulkan.cpp:1520`) | `8` | `9` — fog takes slot 8 |
| both `vkCmdResetQueryPool` calls (`:7330`, `:8326`) | `0, 8` | `0, 9` |
| `uint64_t ts[8]` (`:8113`) | fixed array | `ts[9]` — **`vkGetQueryPoolResults` writes `nq * 8` bytes into it** |
| `uint32_t nq = g.profRasterFrame ? 6u : 8u;` (`:8117`) | `8u` | `9u` on the **non-raster** branch only |
| `double profMs[8]` (`:467`) | all 8 slots already assigned | `profMs[9]`, fog in slot 8 |
| the reset loop `for (int pi = 0; pi < 8; ...)` (`:8168`) | `8` | `9` |
| the printf format + accumulation lines | 8 fields | add the `fog` field |

Then wrap the fog compute with begin/end timestamps and label it `fog` in the `` \ `` overlay.
Grep `\b8\b` around the profiler block before declaring this done — the array sizes are the ones
that bite, because they compile silently. (Contained change, done **with** the perf pass — never
silently skipped, §6.)

- [ ] **Step 5: Build + smoke + tests + toggle test**

```bash
make -C linuxdoom-1.10 -j"$(nproc)" && make -C linuxdoom-1.10 test
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./linuxdoom-1.10/linux/linuxxdoom -iwad wads/doom.wad -warp 1 1 -bootsmoke 105
```
Then on hardware: the `;` key and both menu rows flip **Off→Low→Med→High** cleanly; `rb_fog==0`
visibly removes all fog (proves the gate). Expected: green + `bootsmoke: ... OK`.

- [ ] **Step 6: `-rtverify` (must be green) + `-shotcompare` golden**

```bash
# INV-6 RT self-test — fog lanes sit beyond the 184-byte -rtverify prefix, so this must PASS unchanged:
./linuxdoom-1.10/linux/linuxxdoom -iwad wads/doom.wad -rtverify        # expect: PASS
```
`-shotcompare` (DOOM-0208, config-independent): if fog ships **on-by-default** (`rb_fog=1`),
**re-bless** the golden **with** subtle fog (the canonical pin from Step 1 now includes `rb_fog`),
exactly as DOOM-0183 re-blessed for wet — the gate then guards the fog *look*. If fog ships
**off-by-default**, leave the golden untouched (fog-off is byte-identical, INV-8). Run
`-shotcompare` and confirm it matches the (re-blessed) golden.

- [ ] **Step 7: Perf gate — ≤ 15 % present-total (the pass/fail)**

Per spec §6: average the `` \ `` **backslash** profiler **present-total (ms, not FPS)** over a fixed ~10 s walk
of the **E1M1 green-goo room** (with a sky-hole/doorway in view for shafts), **RT-on, 50 % render
scale**, `rb_fog` **off then on** (same-walk A/B, tee the run log —
`/tmp/doom-ants-run.log`). **Pass:** fog adds **≤ 15 % to present-total** vs the fog-off baseline (spec §6, 2026-07-25);
the goo room is not materially worse than its existing ~40 FPS. **The 60 FPS floor does NOT
bind here:** the spec's 2026-07-25 amendment relaxed it for RT-engaged scenes (the user was
shown the `~45 → ~39 FPS` trade and accepted it), and `docs/standards/performance.md` names
DOOM-0011 §6 as the worked example of that relaxation. The floor still binds Classic and the
raster path, which this feature does not touch (INV-7).

**If it fails**, pull levers in order (§6), re-measure after each: reduce `kFogSteps`; add the
nearest-~4 emitter pruning (L3 note) / drop the emitter loop cost; tighten `kFogMaxDist`; make
mode 4 half-res too (Q4). Do **not** ship over the gate.

- [ ] **Step 8: Commit + flip roadmap + changelog**

```bash
git add linuxdoom-1.10/r_vulkan.cpp linuxdoom-1.10/m_misc.c linuxdoom-1.10/m_menu.c \
        linuxdoom-1.10/i_video.c
git commit -m "DOOM-0011: L6 rb_fog dial + menu + ; key + profiler slot + perf gate"
```
Then flip `DOOM-0011` 🚧→✅ in `ROADMAP.md`, add a `CHANGELOG.md` entry, and (public repo) push.
Update the memory file `doom-0011-volumetrics-design.md` to "shipped".

---

## Self-review (checked against the spec)

**Spec coverage** — COMPLETE as of 2026-07-26, when L1c and L1d were written. §4.3b (the Silent
Hill 2 look) → **L1c**; §4.3a's 2026-07-25 seep amendment → **L1d**; §5's two new sampled images
+ the transform UBO + the set-0 plumbing → **L1c** (noise volume, plumbing) + **L1d** (field, UBO).
The rest:
- §4.1 hook / `FogHit` → L1 (struct + call sites, both modes). §4.2 march (steps, dither,
  early-out, HG) → L1 (march) + L2 (phase) + L5 (dither tune). §4.3 density/pooling/colour → L3
  (pooling) + L4 (tint). §4.4 sky shafts (`kSunDir`, one ray, no-sky case) → L2; torch shafts
  (static slice, nearest-few, no occlusion) → L3. §4.5 profiles (clear/goo/hell, the concrete hell
  rule) → L4. §4.6 half-res + per-mode composite + sky-seam bilinear fallback → L1 (skeleton +
  fallback) + L5 (position-guided bilateral). §5 data (fog image + bindings, `misc6.z/.w`, `rb_view_t` field,
  `rb_fog`, seven menu edits, `;` key) → L1 (image) + L4 (`rb_view_t`/`misc6.w`) + L6 (dial/menu/key).
  §6 perf (profiler slot + ≤ 15 % gate) → L6. §8 **INV-1..12** → Global Constraints (INV-1..8) +
  the per-task guards listed in the next bullet (INV-9..12).
- **Invariant coverage — all twelve are now pinned.** INV-1..8 in Global Constraints, re-stated
  at their tasks (INV-2 in L3, INV-4 in L1, INV-5/7 in L6, INV-6 global, INV-8 in L6 gate).
  The four added by the amendments land in the new tasks: **INV-9** (`skyExposure` gates the
  sky-sourced term only) in L1b's `sigma` note, L1d Step 4 and L4's split step — and, since
  2026-07-27, its *measurement* clause is pinned by DOOM-0276's mask channel rather than by
  L1b Step 2's up-ray; **INV-10** (the
  sky closed form stays wisp-free, and applies the height pool at a single constant height so it
  is still a closed form — re-amended 2026-07-27) in L1c Step 4; **INV-11** (`kWispAmp = 0` is an
  exact no-op) as L1c Step 7's by-construction check; **INV-12** (the seep never leaks into a
  sealed room) in L1d Steps 1–2 and its Step 7 acceptance.

**Placeholder scan** — the `kFog*`/tint/`kHaze*` values are concrete starting numbers explicitly
labelled *tune-on-hardware* (a spec requirement, not a TODO). **Seven shader helpers are new
functions this plan authors**, not existing interfaces: `wisp` (L1c Step 3), `worldToSeepUV`
(L1d Step 3), `sunRayMissesGeometry` (L2 Step 1), `fogHeightPool` (specified at L3 Step 1 but
**not** shipped when that step landed early — L4 Step 3 must extract it), `emitterCentroid`
and `emitterLe` (L3 Step 2), and `fetchFog` (L5 Step 1, alongside renaming the shipped
the shipped `fetchFogBilinear` to `fetchFogBilinearPlain` — **both shipped 2026-07-27**). What
already exists is the *pattern* each is built
from — the ray-query call shape, the emitter-record read, the bilinear fetch — and the plan makes
reading that pattern its own step rather than restating it from memory. So nothing is invented out
of thin air, but seven genuinely new helpers get written: a real cost, not a placeholder-free
claim.

**Type consistency** — `marchFog(vec3,vec3,float,FogHit) → vec4 (inscatter.rgb, transmittance)` is
fixed in L1 and unchanged throughout. `FogHit` starts at L1 as
`{vec3 hitP; vec3 gnormal; uint matFlags;}` and is consumed unchanged through L3; **L4 Step 4
widens it with `uint ctrlFlags`** — the `MatCtrl.flags` word, because `matFlags` carries only
per-vertex flags and the liquid bit is not in it — updating both call sites in that same task.
`rb_fog` (int 0..3) ↔ `pc.misc6[2]` (uint) ↔ `fogNames[]` (4 entries) ↔ `M_ChangeFog`
(`% 4`) are consistent across L1/L6. `misc6.w` = `rb_view_t.hazeDensity` bit-cast float — written
from `g.lastView.hazeDensity` in `RecordRtTrace` (there is no bare `view` there), read back as
`uintBitsToFloat(pc.misc6[3])` — consistent L4.

**Known open items surfaced to the user.** One is a blocker; the rest are not.

- ⛔ **Δ is MEASURED and it failed the gate** (2026-07-27, spec §6): **+8.38 ms / +34.7 %**
  present-total, 95 % of it inside `marchFog`. L1c's gate was `8 % − Δ(L1b)`, so it is negative and
  **L1c is blocked behind a perf pass**, not the other way round. First lever: **DOOM-0276**, the
  open-sky up-ray → a mask channel on L1d's field (spec §4.3a, 2026-07-27 amendment); it supersedes
  Step 2 of L1b above.
- **Q10** — fog on/off by default. The plan ships `rb_fog=1`; a one-line flip if review prefers 0.
- **Q23** — torch-emitter selection, per sample or per ray. L3's two-pass loop still scans every
  static emitter once per sample; the per-ray fallback is named but needs a measurement to decide.
- **Q25 / Q26** — the **two-layer fog** (spec §4.3c, DOOM-0272), added 2026-07-27 after the user
  signed off the single-layer version and asked for a short-range floor fog on top of it. **Q25**:
  does the floor layer need its own open-sky test, or does sharing `skyExposure` suffice? **Q26**:
  `kFloorFogRange` against `kFogSteps` — 24 steps over 2048 units is 85 units apart, so a short
  range is resolved by two or three samples and will band. **Q26 is CLOSED and SHIPPED
  (2026-07-27), ahead of the floor fog:** the march warps its samples toward the camera,
  `t = tMax·s²` with the Jacobian — measured 8-37 % error → 0.1-0.5 % at the *same* 24 steps,
  where 64 uniform steps still band. It went first because it changes the already-accepted look
  by itself. Q25 remains a hardware judgement — the three constants (`kFloorFogDensity` = 0.010,
  `kFloorFogPool` = 24, `kFloorFogRange` = 256) are first guesses with the arithmetic in spec
  §4.3c. **Task L1e** now carries the outdoor half (four steps, no new resource and no new ray);
  the indoor half rides on L1d.
- **Q24 / Q24a** — the sky's haze. **Q24a shipped 2026-07-27**: the backdrop now has its own
  distance, `kFogSkyDist`, because sharing `kFogMaxDist` left the mountains reading as *nearer*
  than a wall — since superseded by the geometric slant path, which makes the inversion impossible
  by construction and demotes `kFogSkyDist` (now 2048) to the layer's horizontal extent. **Q24 is the same lever from the density side and is still open**: when
  L1c doubles `kFogBaseDensity` the mountains **will** move and there is no one-constant fix:
  since 2026-07-27 the sky's path is geometric, so `kFogSkyDist` only sets the horizon's soft cap.
  Levers are `kFogPoolHeight` or a sky-only density; never `kFogBaseDensity`, which would undo
  L1c's foreground tuning.
- **One stale comment in shipped source** — `svgf_composite.comp`'s comment above
  the fog fetch called the L5 upsample "depth-guided". **FIXED 2026-07-27** when L5 Step 1
  shipped: it now reads "Position-guided", changed in the same edit as the rename.
