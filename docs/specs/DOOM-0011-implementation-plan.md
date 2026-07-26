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
> - **This plan predates two spec amendments** (2026-07-24 open-sky standard, 2026-07-25
>   Silent Hill 2 look + seep). **It has no `L1c` and no `L1d` task**, which is exactly the
>   work the spec's §7 says comes next. **Do not proceed past L1b using this document** —
>   the L1c/L1d tasks must be written from the spec's §7 first.
> - **The spec is the authority on every number.** Where this plan and the spec disagree,
>   the spec wins. The known-stale classes were swept on 2026-07-26 (perf gate, sky
>   in-scatter tone, the `σ` split, the sky-visibility mechanism, the profiler key,
>   invariant count, `file:line` citations), but this plan is **not** cold-eyes-converged
>   in its own right.
> - Spec cold-eyes status: the original design converged in 4 loops; the **2026-07-25
>   amendment has run 4 loops** and its log lives in the spec's header.

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
| `shaders/pathtrace.comp` | `marchFog()` definition + call site; mode-4 in-megakernel apply; mode-6 half-res write | L1–L5 |
| `shaders/pt_common.glsl` | Fog `const`s (steps, density, tints, `kSunDir`), phase/density helpers | L1–L5 |
| `shaders/svgf_composite.comp` | Mode-6 apply: fold fog after albedo re-multiply + on sky-passthrough; **plain bilinear** upsample (L1) → depth-guided bilateral (L5) | L1, L5 |
| `r_vulkan.cpp` | New half-res fog image + bindings; `rb_fog` extern; `misc6.z/.w` writes; profiler slot | L1, L4, L6 |
| `r_mesh.h` | New `rb_view_t.hazeDensity` field | L4 |
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

**A note on line numbers:** every `file:line` below was current at plan time, but **earlier tasks
shift later line numbers**. Locate each insert by the **quoted surrounding code**, not the raw
line number.

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

Place near `SKY_COLOR` (`:31`), matching the existing const style:

```glsl
// DOOM-0011: volumetric fog (single-scatter view-ray march). All tune-on-hardware.
const int   kFogSteps        = 24;               // fixed sample count (coherent, cheap)
const float kFogMaxDist      = 2048.0;           // clamp tHit so a long corridor can't blow budget
const float kFogBaseDensity  = 0.0008;           // small always-on "clear air" so shafts read (SHIPPED value; L1c raises to ~0.0016)
const float kFogPoolHeight   = 48.0;             // e-fold height (DOOM units) for floor pooling
const float kFogAnisotropy   = 0.40;             // Henyey-Greenstein g (mild forward bias); 0 = isotropic
const vec3  kSunDir          = normalize(vec3(0.30, 0.30, 1.0)); // world; +z is up (floor = hitP.z). L2.
const vec3  kGooTint         = vec3(0.35, 0.85, 0.30); // sickly green (L4)
const vec3  kHellTint        = vec3(0.90, 0.35, 0.30); // faint red   (L4)
const float kSkyShaftStrength   = 1.0;           // sky in-scatter gain (L1/L2)
const float kTorchShaftStrength = 1.0;           // static-emitter in-scatter gain (L3)

// Henyey-Greenstein phase (forward/back scatter weight); cosTheta = dot(viewDir, lightDir).
float fogPhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
}

// L1: base density only (height pooling + profiles arrive at L3/L4).
float fogDensity(vec3 p) {
    return kFogBaseDensity;
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

    for (int i = 0; i < kFogSteps; ++i, t += dt) {
        vec3  p     = ro + rd * t;
        float sigma = fogDensity(p);
        vec3  Ls    = skyAmbient;                        // L2 adds directional sky + torches
        inscatter += trans * sigma * Ls * dt;
        trans     *= exp(-sigma * dt);
        if (trans < 0.003) break;                        // thick fog occludes the rest cheaply
    }
    return vec4(inscatter, trans);
}
```

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
half-res **bilinear** fetch of the fog target (depth-guided upsample arrives at L5; L1 uses plain
bilinear). Surface branch — replace `:88-91`:

```glsl
        L = albedo * illum + emis * emisMask * ga.a;   // existing, still linear
    }
    if (pc.misc6[2] != 0u) {                            // rb_fog; 0 = no fetch, no change (INV-8)
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
        if (pc.misc6[2] != 0u) {
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
Expected: build green, tests green, `bootsmoke: 105 tics simulated OK, exiting.`

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
Solid **RT engaged only** (modes 4 + 6); `rb_fog`-gated (fog off = byte-identical, INV-8).

**Files:**
- Modify: `shaders/pt_common.glsl` — add `kIndoorFogScale` const; (fallback only) `FLAG_OUTDOOR`.
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

The up-ray is a straight-up (`+Z`) shadow ray with cull mask `0x01`. It cannot hit the mask-`0x04`
sky backdrop, so **MISS = open sky, hit = indoor** (spec §4.3a). Mirror the existing `occluded()`
ray-query init (`pt_common.glsl:189-195`). Because `marchFog` runs in the megakernel it already has
the TLAS in scope; if `occluded()` is directly callable pass it `p`, `vec3(0,0,1)`, a large `tMax`.
Change the `sigma` line (`:789`) from `float sigma = fogDensity(p) * strength;` to:

```glsl
        // §4.3a open-sky exposure: up-ray misses all solid geometry => under open sky.
        bool  openSky = !occluded(p, vec3(0.0, 0.0, 1.0), kFogMaxDist);  // 0x01 mask; sky is 0x04
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

The sky backdrop is open-sky by definition, so give it full fog over `[0, kFogMaxDist]`. The sky
ray sees constant outdoor density, so a **closed form** is exact (no second march loop):
`trans = exp(-kFogBaseDensity * strength * kFogMaxDist)`, `inscatter = SKY_COLOR * (1 - trans)`.
In the **mode-6 sky branch** (`~:1283-1292`), which currently writes `gpos.w=-1` + the sky into
`gillum` and returns without touching `fogImg`, add — under `if (pc.misc6[2] != 0u)` and the same
even/even half-res gate the surface path uses — an `imageStore(fogImg, ivec2(px)/2, vec4(inscatter, trans))`
so the composite's **existing** `fetchFogBilinear` fold on the sky-passthrough branch
(`svgf_composite.comp:100-103`, unchanged) picks it up. (svgf_composite.comp has no `pt_common`
consts, so the value MUST be computed here — spec §4.6a.)

- [ ] **Step 4: Sky-backdrop aerial fog — mode 4 (`pathtrace.comp` sky branch)**

In the mode-4 sky branch, after `colour = skyPanorama(...)` (`:1328`) and under `if (pc.misc6[2] != 0u)`,
fold the same closed-form fog before the write, in the same linear space as §4.6a:

```glsl
            colour = skyPanorama(px, w, h);
            if (pc.misc6[2] != 0u) {
                float strength = fogStrengthScale(pc.misc6.z);
                float trans    = exp(-kFogBaseDensity * strength * kFogMaxDist);
                colour = colour * trans + SKY_COLOR * (1.0 - trans);   // aerial haze on the mountains
            }
```

Then reconcile the old screen-space `SKY_FOG_COL` band (`:761-763`): with real distance-fog now on
the sky, dial `SKY_FOG_COL`'s `smoothstep` contribution down or remove it so the horizon is not
**double-hazed** (spec Q14) — verify by eye at Step 7.

- [ ] **Step 5: Build + smoke + tests**

```bash
make -C linuxdoom-1.10 -j"$(nproc)" && make -C linuxdoom-1.10 test
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./linuxdoom-1.10/linux/linuxxdoom -iwad wads/doom.wad -warp 1 1 -bootsmoke 105
```
Expected: build green (SPIR-V compiles — catches GLSL errors), tests green,
`bootsmoke: 105 tics simulated OK, exiting.`

- [ ] **Step 6: Hardware perf spot-check (RX 6600) — which exposure method ships**

Per spec §6: with the profiler (`` \ `` **backslash** key — `` ` ``/`~` is the RT view cycle), A/B the **added** present-total (fog-off vs fog-on,
same walk) in the goo room (its ~40 FPS baseline is pre-existing) fits **≤ 4 %** — L1b's slice
of L1c's ≤ 8 % allocation, not the whole-feature gate — AND confirm a **typical non-goo
corridor** scene holds the same ≤ 4 % added share with the up-ray on. (This check originally
read "still holds 60 FPS"; the spec's 2026-07-25 amendment relaxed that floor for RT-engaged
scenes, so the share is the only currency now.) **If it holds** →
the per-sample up-ray ships (done). **If it misses** → build the cheap fallback instead:
- `r_mesh.h`: `#define RB_MESH_OUTDOOR 0x100` beside the other `RB_MESH_*` bits (`:82-101`).
- `r_mesh.c`: OR the bit into the `flags` word from `seg->frontsector->ceilingpic == skyflatnum`
  (walls — `emit_wall` has `seg`, near `:275`, or at the 4 call sites `529/554/568/582`) and
  `sec->ceilingpic == skyflatnum` (flats — `emit_subsector_caps`, near `:452`).
- `pt_common.glsl:20-22`: `const int FLAG_OUTDOOR = 0x100;`
- In `marchFog`, replace the per-sample up-ray with `float skyExposure = (h.matFlags & uint(FLAG_OUTDOOR)) != 0u ? 1.0 : kIndoorFogScale;`
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
shaft). Guard the whole sky term on "level has sky" so enclosed levels skip it (§4.4a):

```glsl
        vec3 Ls = vec3(0.0);
        if (skyExists) {                                  // misc4.w != 0xFFFFFFFF
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

Define `sunRayReachesSky()` next to `marchFog()` using the confirmed ray-query helper; it traces
from `p + kSunDir*eps` along `kSunDir` and returns true iff the closest hit is the sky instance
the ray **misses** all solid geometry (mask `0x01` cannot reach the mask-`0x04` sky
instance, so "reaches sky" *is* "missed everything" — spec §4.4(a)).

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

- [ ] **Step 1: Height pooling in `fogDensity()`**

`marchFog()` must pass the floor reference into density. Compute `floorZ` once in `marchFog()`:
`hitP.z` when the primary hit faces up (`gnormal.z > 0.7`, a floor), else a level-min fallback
const `kFogFloorFallback`. Then:

```glsl
float fogDensity(vec3 p, float floorZ) {
    float pool = exp(-max(0.0, p.z - floorZ) / kFogPoolHeight);   // denser near the floor
    return kFogBaseDensity * pool;
}
```
Update the call in `marchFog()` to `fogDensity(p, floorZ)` and add `const float kFogFloorFallback`
to `pt_common.glsl` (tune-on-hardware; start at a low world Z).

- [ ] **Step 2: Torch shafts — nearest-few static emitters, no occlusion (Q2 start cheap)**

In `marchFog()`'s loop, after the sky term, add contributions from the **static** slice only. Do
**not** shadow-test every emitter every sample. Pick the nearest few (by distance to the emitter
centroid) and add each as `Le · falloff(dist) · phase · kTorchShaftStrength`:

```glsl
        // Torch shafts: static emitters [0, omniStart) only (INV-2). Nearest-few, no occlusion (Q2).
        uint omniStart = pc.misc4[1];
        for (uint k = 0u; k < omniStart; ++k) {           // consider only static emitters
            vec3  c   = emitterCentroid(k);               // from the Emitters buffer (:52-56)
            vec3  toL = c - p;
            float d2  = dot(toL, toL);
            // (Optional refinement: keep only the nearest ~4 via a small running set; start = all-static
            //  distance-weighted, which is fine for DOOM's modest static-emitter counts.)
            float falloff = 1.0 / (1.0 + d2 * kTorchFalloff);
            float ph = fogPhaseHG(dot(rd, normalize(toL)), kFogAnisotropy);
            Ls += emitterLe(k) * falloff * ph * kTorchShaftStrength;   // NO occlusion ray in v1
        }
```
Add `const float kTorchFalloff` to `pt_common.glsl`. `emitterCentroid`/`emitterLe` = small helpers
reading the record fields (copy the offsets from the existing emitter read you found).

> **Perf note for the implementer:** `steps × omniStart` phase evals is the cost pole. If a level's
> static-emitter count makes this heavy, add the "nearest ~4" pruning (a fixed-size running-min set
> over the loop) before L6 — it is the first perf lever (§6). Measure at L6, don't pre-optimise.

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
- Modify: `r_vulkan.cpp` (write `view.hazeDensity` bit-cast into `misc6.w`)
- Modify: `shaders/pathtrace.comp` (`marchFog()`: pick profile, apply `mediumTint`, add haze)

**Interfaces:**
- Consumes: `FogHit.matFlags` (now read for goo); `RB_FLAG_LIQUID_NUKAGE = 8u` (`rb_materials.h:17`);
  `kGooTint`, `kHellTint`; `misc6.w` (haze density, bit-cast float).
- Produces: final coloured `inscatter`. Nothing later consumes new interfaces.

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
    boolean hell = (gamemode != commercial && gameepisode >= 3)
                || (gamemode == commercial && gamemap  >= 20);
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
    float haze = view.hazeDensity;                 // DOOM-0011: hell haze -> misc6.w (bit-cast float)
    std::memcpy(&pc.misc6[3], &haze, sizeof(float));
```

- [ ] **Step 4: Apply profiles + tint in `marchFog()`**

Read the profile from the primary hit + haze, set `mediumTint` and a density multiplier, add the
global haze to base density, and multiply every `Ls` contribution by `mediumTint`:

```glsl
    // Profile select (§4.5): default clear; goo if primary hit is liquid nukage; hell haze global.
    vec3  mediumTint = vec3(1.0);
    float densMul    = 1.0;
    float haze       = uintBitsToFloat(pc.misc6[3]);          // hell haze (0 on non-hell)
    if ((h.matFlags & RB_FLAG_LIQUID_NUKAGE) != 0u) {         // rb_materials.h:17, = 8u
        mediumTint = kGooTint;
        densMul    = kGooDensityMul;                          // thicken (tune-on-hardware)
    }
    if (haze > 0.0) {
        mediumTint *= kHellTint;                              // faint red over whatever we have
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
    float skySigma  = kFogBaseDensity * skyExposure;   // outdoor haze, gated
    float areaSigma = (kAreaDensity * gooMult) + haze; // profiles, NOT gated
    float sigma     = (skySigma + areaSigma) * pool * wisp * strength;
```
- **Verify by construction, not by eye:** with `rb_fog` on, a goo room under a solid roof
  must show green fog at a density independent of `kIndoorFogScale` — temporarily setting
  `kIndoorFogScale = 0` must NOT remove the goo pool. If it does, the split did not land.
- Multiply the sky term **and** each torch term by `mediumTint` (colour = light × medium): so
  `Ls += skyRadiance() * kSkyShaftStrength * ph * mediumTint;` and likewise for torches.
- Add `const float kGooDensityMul` to `pt_common.glsl`.

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

## Task L5 — Denoise / quality pass: depth-guided upsample, dither + phase tune

**Goal:** Make the fog **smooth, not grainy or crawling** in a slow pan, holding shaft shape. Swap
L1's plain-bilinear upsample for a **depth-guided bilateral** one, with the sky-seam bilinear
fallback; tune dither, phase, anisotropy.

**Files:**
- Modify: `shaders/svgf_composite.comp` (`fetchFogBilinear` → depth-guided bilateral upsample)
- Modify: `shaders/pathtrace.comp` / `pt_common.glsl` (dither + `kFogAnisotropy` tuning only)

**Interfaces:**
- Consumes: the fog image (L1), the gbuffer depth (`gpos`, already read in `svgf_composite.comp`),
  the sky sentinel `gp.w < 0.0` (`:66`).
- Produces: the final upsampled fog fetch used by both composite branches. No new interfaces.

**Existing code to read first:**
- `svgf_composite.comp:93` — the `gp.w < 0.0` sky sentinel; `:53-66` — `fetchFogBilinear`'s
  four-texel load (the bilateral guide + the
  fallback trigger).
- `r_vulkan.cpp:7561-7568` — the a-trous passes (the **escalation** path, Q6, only if bilateral crawls).

- [ ] **Step 1: Depth-guided bilateral upsample**

Replace `fetchFogBilinear` with a bilateral fetch: sample the four half-res fog texels around `p`,
weight each by `exp(-|depth_full − depth_half| / kFogDepthSigma)` (reject neighbours across a big
depth step, so a shaft against a near wall doesn't bleed onto far geometry). **At sky / far-depth
pixels** (`gp.w < 0.0`, §4.6) a depth guide has no valid neighbour depth at the sky/wall seam →
**fall back to plain bilinear** there (no depth weighting), keeping the shaft-against-sky
reconstruction smooth:

```glsl
vec4 fetchFog(ivec2 p, float depthFull) {
    if (depthFull < 0.0) return fetchFogBilinearPlain(p);   // sky seam: no depth guide (§4.6)
    // else: 4-tap depth-weighted bilateral over the half-res fog target
    ...
}
```
Add `const float kFogDepthSigma` (tune). Wire both composite branches to `fetchFog(p, gp.w)`.

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
git commit -m "DOOM-0011: L5 denoise pass — depth-guided fog upsample + dither/phase tune"
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

- [ ] **Step 2: Menu rows (seven edits + name table — clone `rb_detile`, place like `rb_wet`)**

Per spec §5 (all seven — adding only the menuitem arrays ships a blank row; the seventh is
the **forward declaration** `void M_ChangeFog(int choice);` beside `M_ChangeWet` at
`m_menu.c:224`, without which the file does not compile):
1. `ef_fog` in `effects_e`, `vid_fog` in `videoitem_e`.
2. Row in `EffectsMenu[]` and `VideoMenu[]`, both bound to `M_ChangeFog`.
3. `M_DrawEffectsMenu`: a `"Volumetric fog:"` label + `fogNames[rb_fog]` value keyed on `ef_fog`
   (mirror the `"De-tile:"` row, **not** the boolean `"Wet liquid:"`).
4. `videoLabels[]` entry for `vid_fog`.
5. `M_VideoCrispValue`: `case vid_fog: return fogNames[rb_fog];` (mirror `case vid_detile:`).
6. `M_ChangeFog` mirroring `M_ChangeDetile` (`rb_fog = (rb_fog + 1) % 4;`), plus
   `static const char *fogNames[] = {"Off","Low","Med","High"};`.

- [x] **Step 3: `;` hotkey** — DONE EARLY in commit `f8c6b1f` (`SDLK_SEMICOLON` cycles
  Off/Low/Med/High in `i_video.c`, mirroring the `]`/`[`/`'` toggles). Nothing left here.

- [ ] **Step 4: Profiler slot for the fog pass**

Bump `queryCount` (`r_vulkan.cpp:1520`) from 8 to **9** (fog takes slot 8); widen the two resets
(`:7330`, `:8326`, both hardcoding `0, 8`) and the readback (`:8118`); wrap the fog compute with begin/end timestamps.
Label it `fog` in the `` \ `` profiler overlay. (Contained change, done **with** the perf pass — never
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

**Spec coverage** — PARTIAL. §4.3a's 2026-07-25 seep amendment and §4.3b (the Silent Hill 2
look) have **no task**, because L1c/L1d were never written; everything below describes the
coverage that does exist:
- §4.1 hook / `FogHit` → L1 (struct + call sites, both modes). §4.2 march (steps, dither,
  early-out, HG) → L1 (march) + L2 (phase) + L5 (dither tune). §4.3 density/pooling/colour → L3
  (pooling) + L4 (tint). §4.4 sky shafts (`kSunDir`, one ray, no-sky case) → L2; torch shafts
  (static slice, nearest-few, no occlusion) → L3. §4.5 profiles (clear/goo/hell, the concrete hell
  rule) → L4. §4.6 half-res + per-mode composite + sky-seam bilinear fallback → L1 (skeleton +
  fallback) + L5 (bilateral). §5 data (fog image + bindings, `misc6.z/.w`, `rb_view_t` field,
  `rb_fog`, seven menu edits, `;` key) → L1 (image) + L4 (`rb_view_t`/`misc6.w`) + L6 (dial/menu/key).
  §6 perf (profiler slot + ≤ 15 % gate) → L6. §8 INV-1..8 → Global Constraints + per-task guards.
- **Invariant coverage is PARTIAL, and this is the plan's largest gap.** Only **INV-1..8**
  are pinned in Global Constraints and re-stated at their tasks (INV-2 in L3, INV-4 in L1,
  INV-5/7 in L6, INV-6 global, INV-8 in L6 gate). The spec now carries **twelve**: INV-9/10
  (added 2026-07-24) and INV-11/12 (added 2026-07-25) are owned by the **L1c/L1d tasks that
  this plan does not yet contain** — INV-9 appears once in passing (L1b's `sigma` note) and
  INV-10/11/12 appear nowhere. Writing those tasks is what closes this.

**Placeholder scan** — the `kFog*`/tint/`kHaze*` values are concrete starting numbers explicitly
labelled *tune-on-hardware* (a spec requirement, not a TODO). Shader helper calls
(`sunRayReachesSky`, `emitterCentroid`, `emitterLe`, ray-query pattern) are flagged
"read the existing helper first, confirm its signature" rather than invented — these consume
**existing** engine interfaces the plan cannot restate without reading them; that read is a named
step, not a placeholder.

**Type consistency** — `marchFog(vec3,vec3,float,FogHit) → vec4 (inscatter.rgb, transmittance)`
and `FogHit {vec3 hitP; vec3 gnormal; uint matFlags;}` are fixed in L1 and consumed unchanged by
L2–L4. `rb_fog` (int 0..3) ↔ `pc.misc6[2]` (uint) ↔ `fogNames[]` (4 entries) ↔ `M_ChangeFog`
(`% 4`) are consistent across L1/L6. `misc6.w` = `view.hazeDensity` bit-cast float, read
`uintBitsToFloat(pc.misc6[3])` — consistent L4.

**Known open items surfaced to the user (not blockers):** Q10 (fog on/off default — plan ships
`rb_fog=1`, one-line flip if review prefers 0) and the code-side stale-comment sweep noted below.
