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

**Spec:** `docs/specs/DOOM-0011-volumetric-lighting.md` (cold-eyes converged, 4 loops). Read it
in full before starting — this plan implements it; every `§`/`INV`/`Q` reference points there.

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
  (`svgf_composite.comp:88`) — it never rides `illum`/`gillum`. `inscatter`/`transmittance` are
  **linear radiance**, folded before the tonemap on **both** the surface path **and** the
  sky-passthrough branch (`:66-72`) in the **same** colour space (INV-4).
- **No push-struct growth.** Use exactly `misc6.z` + `misc6.w` (the last two free lanes). Do
  **not** append `misc7`; keep `RtPushConstants` at 240 B (`static_assert` `r_vulkan.cpp:7374`,
  `pcr.size` `:2355`) (INV-5).
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
| `shaders/svgf_composite.comp` | Mode-6 apply: fold fog after albedo re-multiply + on sky-passthrough; bilateral upsample | L1, L5 |
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

Only **L6** adds objective pass/fail gates: `-rtverify` green, `-shotcompare` golden, and the
**≤ 5 % present-total** perf bar. Those are real commands, given in Task 6.

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
- Produces: `vec2 fog = vec4 marchFog(vec3 ro, vec3 rd, float tHit, FogHit h)` returning
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
- `svgf_composite.comp:66-72` — sky-passthrough branch (`if (gp.w < 0.0)`).
- `svgf_composite.comp:88` — `L = albedo * illum + emis * emisMask * ga.a;` and `:91`
  `imageStore(outColor, p, vec4(toneEncode(L), 1.0));`.
- `r_vulkan.cpp` — how an existing storage image (e.g. a denoiser target near `:7300`) is created,
  bound in the descriptor set, and reset; copy that pattern for the fog image.

- [ ] **Step 1: Add the fog consts + helpers to `pt_common.glsl`**

Place near `SKY_COLOR` (`:31`), matching the existing const style:

```glsl
// DOOM-0011: volumetric fog (single-scatter view-ray march). All tune-on-hardware.
const int   kFogSteps        = 24;               // fixed sample count (coherent, cheap)
const float kFogMaxDist      = 2048.0;           // clamp tHit so a long corridor can't blow budget
const float kFogBaseDensity  = 0.015;            // small always-on "clear air" so shafts read
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
`:7300`). Add: one `RGBA16F` image at **half render resolution** (`renderExtent/2` rounded up),
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

Sky-passthrough branch (`:66-72`) — fold fog treating the display-encoded sky as linear, then
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

## Task L2 — Sky shafts: directional sun + per-sample sky-visibility ray + HG phase

**Goal:** Turn the flat glow into **slanted beams**. Add the sun direction, cast one shadow ray
toward it per sample, and weight the in-scatter by the HG phase so shafts read as beams.

**Files:**
- Modify: `shaders/pathtrace.comp` (`marchFog()` inner loop: sun-visibility ray + phase)
- Modify: `shaders/pt_common.glsl` (only if a phase/helper tweak is needed — consts already exist)

**Interfaces:**
- Consumes: `kSunDir`, `fogPhaseHG`, `kFogAnisotropy` (L1); the existing sky-visibility test
  (`pathtrace.comp:816-817`, TLAS custom-index 2) + whatever shadow/any-hit ray-query helper the
  primary trace uses.
- Produces: directional `Ls` in `marchFog()` (torch sources add to it at L3).

**Existing code to read first:**
- `pathtrace.comp:816-817` — the sky-instance test (custom-index 2). Reuse the **same** predicate
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
            if (sunRayReachesSky(p, kSunDir)) {           // one ray; reuse the :816-817 sky test
                float ph = fogPhaseHG(dot(rd, kSunDir), kFogAnisotropy);
                Ls += skyRadiance() * kSkyShaftStrength * ph;   // skyPanorama()/SKY_COLOR
            }
        }
        // L3 adds torch contributions to Ls here.
```

Define `sunRayReachesSky()` next to `marchFog()` using the confirmed ray-query helper; it traces
from `p + kSunDir*eps` along `kSunDir` and returns true iff the closest hit is the sky instance
(custom-index 2) or the ray misses into sky.

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
    if ((h.matFlags & 8u) != 0u) {                            // RB_FLAG_LIQUID_NUKAGE
        mediumTint = kGooTint;
        densMul    = kGooDensityMul;                          // thicken (tune-on-hardware)
    }
    if (haze > 0.0) {
        mediumTint *= kHellTint;                              // faint red over whatever we have
    }
```
- Fold `densMul` and `haze` into density: `return (kFogBaseDensity + haze) * pool * densMul;`
  (thread `densMul`/`haze` into `fogDensity`, or apply at the call site — keep it one place).
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
- `svgf_composite.comp:58-72` — `gpos`/`gp.w` load + sky sentinel (the bilateral guide + the
  fallback trigger).
- `r_vulkan.cpp:7545` — the a-trous passes (the **escalation** path, Q6, only if bilateral crawls).

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
**≤ 5 % present-total** vs fog-off. This is the only task with hard pass/fail criteria.

**Files:**
- Modify: `r_vulkan.cpp` (`rb_fog` extern; `misc6.z` write; profiler `queryCount`+resets+readback;
  DOOM-0208 canonical-config pin)
- Modify: `m_misc.c` (`rt_fog` config default)
- Modify: `m_menu.c` (two menu rows, `M_ChangeFog`, `fogNames[]`, draw/label/value sites)
- Modify: `i_video.c` (`;` hotkey)

**Interfaces:**
- Consumes: `misc6.z` (already read by the shaders since L1 as `pc.misc6[2]`); the DOOM-0208 arm
  block (`r_vulkan.cpp:8177`).
- Produces: the shipped `rb_fog` dial (0..3) driving all fog cost.

**Existing code to read first:**
- `r_vulkan.cpp:1000` — `extern "C" { int rb_wet = 1; }` (place `rb_fog` beside it).
- `r_vulkan.cpp:7427` — `pc.misc6[1] = rb_wet ? 1u : 0u;` (place the `rb_fog` write beside it).
- `r_vulkan.cpp:8177` — DOOM-0208 canonical-config arm block (pin `rb_fog` here).
- `r_vulkan.cpp:1510` `queryCount = 8` (profiler pool, full); `:7300`, `:8285` resets;
  `:8076-8086` readback — the four sites to widen for the fog timer slot.
- `m_misc.c:270` — `{"rt_wet", &rb_wet, 1}` config row (add `rt_fog` beside it).
- `m_menu.c` — the `rb_detile` multi-value pattern to clone: `effects_e`/`videoitem_e` enums
  (`:501-510`/`:543-565`), `EffectsMenu[]`/`VideoMenu[]` (`:512-520`/`:567-588`),
  `M_DrawEffectsMenu` `"De-tile:"`+`detileNames[]` row (`:1465`), `videoLabels[]` (`:1491`),
  `M_VideoCrispValue` `case vid_detile:` (`:1558`), `M_ChangeDetile`.
- `i_video.c:441-475` — the toggle-key block (`]`/`[`/`'`/`~`/`` ` ``); `;` (`SDLK_SEMICOLON`) is free.

- [ ] **Step 1: `rb_fog` extern + config + push write + canonical pin**

- `r_vulkan.cpp` beside `rb_wet` (`:1000`): `extern "C" { int rb_fog = 1; }`
  (subtle "Low" on by default — matches `rb_wet=1`/`rb_filth=1`/`rb_detile=2`; **Q10** — flip to
  `0` if review prefers off-by-default; if `0`, skip the golden re-bless in Step 6).
- `m_misc.c` beside `rt_wet` (`:270`): `{"rt_fog", &rb_fog, 1}` (match the `rb_fog` default).
- `r_vulkan.cpp` beside `misc6[1]` (`:7427`): `pc.misc6[2] = (uint)rb_fog;` (replaces the `= 0u`).
- `r_vulkan.cpp` DOOM-0208 arm block (`:8177`): pin `rb_fog` to its shipped default alongside
  `rb_detile=2, rb_filth=1, rb_wet=1`.

- [ ] **Step 2: Menu rows (six edits + name table — clone `rb_detile`, place like `rb_wet`)**

Per spec §5 (all six — adding only the menuitem arrays ships a blank row):
1. `ef_fog` in `effects_e`, `vid_fog` in `videoitem_e`.
2. Row in `EffectsMenu[]` and `VideoMenu[]`, both bound to `M_ChangeFog`.
3. `M_DrawEffectsMenu`: a `"Volumetric fog:"` label + `fogNames[rb_fog]` value keyed on `ef_fog`
   (mirror the `"De-tile:"` row, **not** the boolean `"Wet liquid:"`).
4. `videoLabels[]` entry for `vid_fog`.
5. `M_VideoCrispValue`: `case vid_fog: return fogNames[rb_fog];` (mirror `case vid_detile:`).
6. `M_ChangeFog` mirroring `M_ChangeDetile` (`rb_fog = (rb_fog + 1) % 4;`), plus
   `static const char *fogNames[] = {"Off","Low","Med","High"};`.

- [ ] **Step 3: `;` hotkey**

In `i_video.c` (`:441-475`), add a `case SDLK_SEMICOLON:` that does `rb_fog = (rb_fog + 1) % 4;`
and prints `Volumetric fog: <fogNames[rb_fog]>` (mirror the `]`/`[` toggles).

- [ ] **Step 4: Profiler slot for the fog pass**

Bump `queryCount` (`r_vulkan.cpp:1510`) by the slots a fog timer needs; widen the two resets
(`:7300`, `:8285`) and the readback (`:8076-8086`); wrap the fog compute with begin/end timestamps.
Label it `fog` in the `` ` `` overlay. (Contained change, done **with** the perf pass — never
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

- [ ] **Step 7: Perf gate — ≤ 5 % present-total (the pass/fail)**

Per spec §6: average the `` ` `` profiler **present-total (ms, not FPS)** over a fixed ~10 s walk
of the **E1M1 green-goo room** (with a sky-hole/doorway in view for shafts), **RT-on, 50 % render
scale**, `rb_fog` **off then on** (same-walk A/B, tee the run log —
`/tmp/doom-ants-run.log`). **Pass:** fog adds **≤ 5 % to present-total** vs the fog-off baseline;
**Ultra holds its 60 FPS floor** and the goo room is not materially worse than its existing ~40 FPS.

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

**Spec coverage** — every spec section maps to a task:
- §4.1 hook / `FogHit` → L1 (struct + call sites, both modes). §4.2 march (steps, dither,
  early-out, HG) → L1 (march) + L2 (phase) + L5 (dither tune). §4.3 density/pooling/colour → L3
  (pooling) + L4 (tint). §4.4 sky shafts (`kSunDir`, one ray, no-sky case) → L2; torch shafts
  (static slice, nearest-few, no occlusion) → L3. §4.5 profiles (clear/goo/hell, the concrete hell
  rule) → L4. §4.6 half-res + per-mode composite + sky-seam bilinear fallback → L1 (skeleton +
  fallback) + L5 (bilateral). §5 data (fog image + bindings, `misc6.z/.w`, `rb_view_t` field,
  `rb_fog`, six menu edits, `;` key) → L1 (image) + L4 (`rb_view_t`/`misc6.w`) + L6 (dial/menu/key).
  §6 perf (profiler slot + ≤5 % gate) → L6. §8 INV-1..8 → Global Constraints + per-task guards.
- **All eight invariants** are pinned in Global Constraints and re-stated at their task
  (INV-2 in L3, INV-4 in L1, INV-5/7 in L6, INV-6 global, INV-8 in L6 gate).

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
