# Renderer & Shaders Standard

Phase 2 evolves DOOM's renderer toward a real hardware path tracer while keeping
the original playable. This standard covers the seam that makes that possible,
how shaders are built, and the one shared resource that bites when it isn't
tracked: the path tracer's push-constant lanes. The *why* behind the core
choices is owned by ADR `docs/decisions/0001-renderer-language-and-api.md`; this
is the working rulebook.

## The three tiers

The player picks one of three ways to draw the world, from the in-game menu:

| Menu name | `rendermode_t` | What it is |
|-----------|----------------|------------|
| **Classic** | `RB_CLASSIC` | the original 1997 software renderer, on the CPU |
| **Solid** | `RB_RASTER3D` | Vulkan rasteriser — the DOOM world on the GPU |
| **Ultra** | `RB_RT3D` | Vulkan hardware **path tracer** — GI, ray-traced shadows |

Solid and Ultra are the **same Vulkan backend** today; the `~` key
(`rb_rtdebug`) switches raster vs. RT *within* that backend, while the menu
selects the tier. Don't assume they are separate code paths.

## The seam

- The engine stays **C**. The Vulkan backend is **C++** (`r_vulkan.cpp`),
  isolated behind the plain-C `renderer_backend_t` function-pointer seam in
  `r_backend.h`. C code never sees C++ or Vulkan types directly.
- A side effect of the software renderer that the 3D backends skip
  (e.g. automap `ML_MAPPED` marking) must be **replicated** in the backend —
  Solid/Ultra bypass `R_RenderPlayerView`, so anything it did as a side effect
  won't happen for free.

## Shaders

- **GLSL, compiled ahead-of-time to SPIR-V** — never compiled at runtime.
- The Makefile runs `glslc --target-env=vulkan1.2 -O` on each shader in
  `linuxdoom-1.10/shaders/`, then `xxd -i -n <name>_spv` embeds the SPIR-V as a
  C byte array (`*.spv.h`) that `r_vulkan.cpp` `#include`s. The shipped binary is
  **self-contained** — no runtime shader files.
- `--target-env=vulkan1.2` is a **floor**, not a pin (the path tracer needs
  SPIR-V 1.5 for `VK_KHR_ray_query` + `buffer_reference`) — see the dependencies
  standard's Permanent constraints.
- Shared shader code lives in `pt_common.glsl`, `#include`d by the compute
  shaders. **`glslc` emits no auto-dependency for a GLSL `#include`**, so when a
  shader includes another, add the dependency explicitly in the Makefile or an
  edit to the included file won't rebuild the dependents.

## Push-constant lanes — a scarce shared resource

The compute path tracer packs all its per-frame scalars into a handful of
`uvec4` push-constant lanes (`misc`, `misc2` … `misc6` in `pathtrace.comp`)
rather than churning descriptors. **There is no free lane.** Two rules:

1. **Before reusing a lane, check its actual usage sites — not the struct
   comment.** The inline comments drift (e.g. `misc2`'s struct comment still
   describes only x/y, but `.z`/`.w` carry the muzzle-flash and flashlight state).
   Grep how each component is read in `pathtrace.comp` / `pt_common.glsl` before
   assuming a slot is spare.
2. **When you add a field, document it in both places** — the `PC` struct comment
   *and* the map below.

Current allocation (verify against `pathtrace.comp` before relying on it):

| Lane | x | y | z | w |
|------|---|---|---|---|
| `misc` | mode | width | height | numWall (flat-id offset) |
| `misc2` | emitter count | probe count | muzzle-flash strobe (float bits; 0 = not firing) | flashlight on/off |
| `misc3` | sample-seed base | spp this dispatch | estimator (0 = power-NEE, 1 = brute-force) | SVGF frame parity (`r_vulkan.cpp:7476`; the G-buffer ping-pong `pathtrace.comp:1195` indexes) |
| `misc4` | sprite material base (numWall+numFlat) | omniStart | numSectors (DOOM-0119 REJECT cull, 0 = off) | sky wall-tex bindless id (0xFFFFFFFF = procedural sky) |
| `misc5` | world-grime overlay bindless id | de-tile quality (0 off / 1 2-tap / 2 4-tap) | dirt-colour texture bindless id | filth master toggle |
| `misc6` | ripple/wisp time (float bits, seconds) — **shared**: DOOM-0183 liquid ripples *and* DOOM-0011 fog-wisp drift, so it must stay written unconditionally | wet toggle | fog strength `rb_fog` 0..3 (DOOM-0011) | hell-haze density (float bits; DOOM-0011, reserved 0 until L4) |

Beside the `uvec4` lanes sit two bare `uint` scalars, `fogFloorZ` and `wispAngle` — both
bit-cast floats, both per-level rather than per-frame. They are not a sixth lane: they are
the two pad words `misc6`'s 16-byte alignment forced anyway, spent instead of wasted, which
is why the push range is still 240 bytes.

| Scalar | Contents |
|--------|----------|
| `fogFloorZ` | outdoor fog-layer altitude, world units (DOOM-0011; `rb_mesh_t::fogFloorZ`) |
| `wispAngle` | fog-wisp drift heading, radians, seeded per map (DOOM-0300; `rb_mesh_t::wispAngle`) |

`misc5`/`misc6` are the DOOM-0179/0181/0183/0011 lanes; the earlier lanes are
DOOM-0009/0100 foundations. **`misc6` is full and so are both pad words** — DOOM-0300 spent
the second one, so there is no longer any slack to borrow. The next RT push value must
append a `misc7 uvec4` (240 → 256 B, the documented device limit).

## RT correctness gate

Any change to the ray-tracing path is verified with the headless `-rtverify`
self-test (the **INV-6** check, DOOM-0009 build step 4d) on the reference GPU
(AMD RX 6600) before it ships. A shipped RT change means `-rtverify` passed.

## The north star

Everything above serves one goal: a Quake-RTX-class overhaul that **still reads
as DOOM**. Effects get tuned with the user against that feel — brighter and
truer light, not a different game.
