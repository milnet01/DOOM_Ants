# DOOM-0310 — Fog density and the fields (DOOM-0011 volumetrics, part 1 of 3)

**Status:** **Extracted 2026-08-03 from `docs/specs/DOOM-0011-volumetric-lighting.md`
§4.3/§4.3a–c.** Everything this part specifies is **shipped and user-signed-off**
(L1, L1b, **L3's height pooling**, L1c, L1d, L1e, plus the DOOM-0276/0281/0292/0300
amendments) **except the area-profile term of §4.1's σ, which is L4 and unbuilt** — this
document exists to pin that term's placement before it is written.

**"L3" is a shared label and this part owns only half of it.** The parent's L3 row covers
*height pooling **and** torch shafts*; the pooling is this part's (§4.2) and the per-cell
torch bake is part 2's — the shader uses `// DOOM-0011 L3:` for both. **L3b** (re-baking the
fog-light grid when the map moves, INV-14) is entirely part 2's and appears nowhere below.

**Parent:** DOOM-0011 remains the umbrella spec — the goal, the scope, the shared
invariants and the whole-feature performance gate stay there. This part owns **density,
colour and the two fields**, and with it **INV-9, INV-11 and INV-12**. Sibling parts, named
by the parent's section numbers: **DOOM-0011 §4.4** (light sources + the bakes) and
**DOOM-0011 §4.6/§4.6a** (resolve + composite); neither has an id yet. §2's table is the
single statement of who owns which invariant — this line does not restate it.

**Beware two numbering spaces.** A bare `§4.4` or `§4.6` in this document means *this
document's* §4.4 (open-sky exposure) and §4.6 (the wisps). The parent's same-numbered
sections are always written **`DOOM-0011 §4.4`** / **`DOOM-0011 §4.6`**.

**Cold-eyes log (rule 14).** This part runs the gate **from loop 1 on its own bytes**.
The parent's 23 loops ran against a document that no longer exists and **none of that
review is inherited**.

| Loop | Tally | Outcome |
|---|---|---|
| **0-split** | — | **Not a review — no reviewer was dispatched.** Extraction of the parent's §4.3/§4.3a–c, with DOOM-0308's verified part-1 findings folded in directly (that bullet is a filed, already-verified list; it was deliberately not re-reviewed). Three incompatible σ statements reconciled to one against the shipped shader; the two superseded statements **deleted, not annotated**. Every constant re-grounded against `pt_common.glsl` at HEAD, which is where the stale-constant findings came from. |
| **1** (2026-08-03, 3 lanes) | **C 3 · H 4 · M 14 · L 6 · INFO 5** — 27 verified / 0 unverified, all fixed | **Not converged — loop 2 owed.** The three CRITICALs were all build-changing and two were the split's own premise failing. **(1)** §4.1's σ wrote `wisp(p,t)`, overloading `t` — the ray distance `σ_floor` uses — onto the drift clock the shipped line passes `rippleTime()` to; σ now declares `p`, `t` and `τ` separately. **(2)** The claim "no second σ anywhere in the split" was false when written: the parent's §4.5 still carried a partial σ that omitted the floor addend and named a `heightPool` factor §4.1 does not have — the exact defect the split exists to end, reduced to a pointer. **(3)** The L4 profile addend's evaluation domain was unspecified, and §3's "density must not depend on what the ray hits" was stated absolutely while DOOM-0011 §4.5 requires goo to be **primary-hit-keyed** — so the document forbade the design it was written to enable; §3 now tables its two declared relaxations and INV-9 carries the carve-out. Worst HIGH: the floor layer's headline figures (37 %, τ 0.46) were computed against a `baseZ` the shipped code does not use outdoors, which oversized part 3's sky-seam obligation ~2.7× — both branches now derived (roofed 37 %, outdoors 16 %) and the "within 2 %" comparison, which mixed two reference heights, deleted. Also: `kAreaDensity`/`areaMult`/`mediumTint` were never named though L4 needs them; INV-4 was owned twice; §5's inventory command could not match its own `#define` rows; 256 KB → 512 KiB. |

---

## Contents

- **§1** Goal
- **§2** Where this sits — the three-way split, who owns the seep field's channels, and the
  two numbering spaces
- **§3** The problem, precisely — and the two declared relaxations of its central rule
- **§4** Design
  - **4.1** σ — the single authoritative statement (and where L4's term attaches)
  - **4.2** The aerial layer
  - **4.3** The floor layer
  - **4.4** Open-sky exposure — the fog-placement standard
  - **4.5** The seep field
  - **4.6** The wisps — the Silent Hill 2 look — **and the fog's colour**, including the
    `mediumTint` rule L4 needs
- **§5** Data & resources — **§6** Performance budget (incl. L4's) — **§7** Build order —
  **§8** Invariants (INV-9, INV-11, INV-12) — **§9** Alternatives considered —
  **§10** Open questions — **§11** What checks this

**Citation convention.** The project rule
(`docs/standards/documentation.md` § *Citing code from docs*): **the symbol or quoted
code is authoritative and a line number is only a hint.** This document cites symbols
deliberately — the parent's bare line numbers had to be re-anchored in four separate
review loops, and 50 of them rotted in one commit pair. Locate `fogDensity`,
`floorFogDensity`, `wisp`, `kSeepMax`, `RB_BuildSeepField` by name.

---

## 1. Goal

Say, once and correctly, **how thick the air is at a point** in the ray-traced view, and
**where that thickness comes from** — so that every layer built on it (the shipped
aerial haze, floor fog, seep and wisps; the unbuilt area profiles) composes into one
expression rather than four that disagree.

Two things follow, and they are the reason this part was split out first:

1. **One σ.** The parent carried **three** incompatible statements of the density
   formula, one of which declared itself authoritative and omitted a shipped term. §4.1
   replaces all three.
2. **L4's term has a forced position.** The area profiles attach **outside** the
   `skyExposure` gate and **inside** `wisp` and the strength dial. That is not a
   preference — §4.4 shows the alternative drives a **sealed** goo room to as little as
   5 % density (`kIndoorFogScale`; a room near an opening grades higher) and so cancels
   the feature exactly where it is wanted.

## 2. Where this sits

### The three-way split

The parent reached 3383 lines and produced ~30 verified findings in a single cold-eyes
loop, the great majority unrelated to whatever change was being gated. The failure mode
is legible in the findings themselves: **amendments superseded earlier text in place**,
so a top-down reader met an abandoned contract first and its retraction a hundred lines
later. All three σ statements were that shape.

| Part | Owns | Invariants |
|---|---|---|
| **DOOM-0310** (this) | density; the aerial + floor layers; open-sky exposure; the seep field; the wisps; the fog's colour and in-scatter brightness | INV-9, INV-11, INV-12 |
| part 2 (DOOM-0011 §4.4) | light sources, shafts, the sun-clearance bake, the fog-light bake | INV-2, INV-3, INV-13, INV-14 |
| part 3 (DOOM-0011 §4.6/§4.6a) | half-res, denoise, composite, the sky backdrop's closed form | INV-4, INV-10 |
| **DOOM-0011** (umbrella) | goal, scope, tiers, the whole-feature perf gate, the profile *selection* of §4.5 | INV-1, INV-5, INV-6, INV-7, INV-8 |

Every id INV-1..INV-14 appears in exactly one row. `rg -o 'INV-[0-9]+' ` over this table
returns 14 distinct ids — recount it after any edit, because a doubly-owned invariant is
one both owners will assume the other maintains.

**Invariant ids are permanent and are NOT renumbered by the split** — they are cited from
shader comments, from CHANGELOG and from the sibling plan. This part restates INV-9,
INV-11 and INV-12 as its own; the parent points here rather than keeping a second copy.

**Their citation sites, since "grep for the id" is not a safe instruction:** INV-11 is cited
from `pt_common.glsl` and `pathtrace.comp`, INV-12 from `pt_common.glsl`, `pathtrace.comp`
and `r_mesh.c`, and **INV-9 from `pathtrace.comp` only**. Ids are **not unique across
specs** — `rg 'INV-9' linuxdoom-1.10/` also returns a DOOM-0183 comment on `LIQUID_LAVA`,
and `rg 'INV-11'` returns two `r_vulkan.cpp` hits belonging to another spec entirely. Read
the surrounding comment, not the id alone.

### Who owns the seep field's four channels

One `RGBA16F` image serves both this part and part 2, and the split must not let them
drift. Ownership is per channel:

| Channel | Content | Owner |
|---|---|---|
| `.r` | `d` — distance to outdoor air **through open space** | **this part** (§4.5) |
| `.g` | open-sky mask — `1` where the cell's sector has `ceilingpic == skyflatnum` | **this part** (§4.4) |
| `.b` / `.a` | the sun-clearance interval `[zLo, zHi]` | part 2 (INV-13) |

The **build, the sampler state, the world→UV transform, the cell-size rule and the void
ring are this part's contract** (§4.5) and part 2 inherits them unchanged. What part 2
adds is two channels and a second tolerance: the seep decides connectivity on the portal
graph *before* rasterising and so does not care whether a cell centre resolves to the
right sector, while the clearance march reads per-cell heights directly and therefore
needs its own void test.

### Dependencies

- **DOOM-0009** (path tracer) — the march lives in `pathtrace.comp`'s `marchFog`, modes
  4 (NEE display) and 6 (denoised play).
- **DOOM-0183** — supplies the drift clock. `wisp` reads `misc6.x`, DOOM-0183's ripple
  time, so the wisps add **no new `uvec4` lane** (INV-5). They are not free of the push
  block, though: DOOM-0300's `wispAngle` spends the **second of the two pad words**
  `misc6`'s alignment had already forced (§4.6), which is the last spare word in the
  240-byte block.

## 3. The problem, precisely

**A participating medium's opacity only ever grows with distance.** So a single density
term cannot satisfy both of the things asked of it: the density that makes the air at
your feet visibly misty is the same density that, integrated over a thousand units,
turns the far end of a courtyard into a white sheet. Every re-balance on 2026-07-27
traded one against the other. §4.3's second layer exists because tuning cannot resolve
this — it is structural.

**Uniform density does not read as fog at all.** With `fogDensity()` returning a
constant, every surface is greyed in proportion to its distance and nothing reads as
standing *in* anything. The user's verdict on that build: *"a fog look is applied to
geometry instead of an actual cloud near the ground. If we can emulate a cloud near the
ground, that should resolve everything."*

**And density must not depend on what the ray hits.** This is the defect that took three
passes on 2026-07-27 to find, and it is the sharpest constraint in this document.
`baseZ` was taken from the primary hit — `hitP.z` when it faced up, else the camera's
floor. Standing on a ledge above a courtyard, that yields **two clouds at two heights in
one view**: wall pixels referenced the ledge so their cloud sat high and hazed the wall,
floor pixels referenced the courtyard so their cloud sat low and the eye looked over the
top of it. The user named it exactly: *"we are not actually rendering a cloud, we are
simulating the look of a cloud but only on some surfaces, not all."*

**The rule has exactly two declared relaxations, and a third would be a defect.** They are
declared because the difference between a relaxation and the pass-3 bug is whether it is
written down and bounded, not whether it touches the view:

| term | depends on | why it is allowed |
|---|---|---|
| `σ_floor`'s range factor | `t`, the ray's own parameter | bounded contribution is the whole point of the layer (§4.3) — and its *height* factor still depends on `p` alone |
| the L4 profile addend | `FogHit.matFlags`, the primary hit | every pixel of a profiled room keys the same way, so the error shows only at a doorway edge (§4.1) |

**`σ_aerial` has none and must keep none** — that is the pass-3 fix itself. Anything that
makes the *aerial* layer's `baseZ` or `poolH` depend on the hit reintroduces two clouds at
two heights in one view.

## 4. Design

### 4.1 σ — the single authoritative statement

**This is the only statement of fog density in this document.** Neither sibling part may
restate it, in whole or in part — §11 records that nothing mechanical enforces that.

```
σ(p, t, τ) = ( ( σ_aerial(p) + σ_floor(p,t) ) · skyExposure(p)     // sky-sourced, §4.2/§4.3/§4.4
             + Σ_profiles areaDensity(profile) · areaMult(profile) ) // room-sourced, L4 — UNBUILT
             · wisp(p, τ)                                           // §4.6
             · fogStrengthScale(rb_fog)                              // the `;` dial
```

**Three free variables, and two of them are easy to conflate — the shipped call passes a
different value to each:**

| symbol | is | source |
|---|---|---|
| `p` | the sample's world position | `ro + rd · t` |
| `t` | **distance along the view ray** — a property of the *view*, not the world | the march's loop variable |
| `τ` | **elapsed seconds** — the drift clock | `misc6.x`, via `rippleTime()` |

`σ_floor` takes `t`; `wisp` takes `τ`. Writing `wisp(p, t)` would advect the billows by
march distance instead of by time, which freezes the drift and smears the noise along
every ray. The shader's `wisp` happens to *name* its own second parameter `t`, which is
why this is worth spelling out rather than leaving to the reader.

Four things the form has to carry, each of which one of the three superseded statements
got wrong:

1. **Two sky-sourced addends, not one.** The aerial layer and the floor layer add, and
   **both** sit inside the `skyExposure` gate.
2. **`skyExposure` gates the sky-sourced terms ONLY — never the area profiles.** §4.4
   derives why, and it is the load-bearing rule of the whole section.
3. **The profile term is a sum over profiles**, each with its own `areaDensity`, because
   goo takes the compile-time `const` **`kAreaDensity`** (start `0.0020`) while hell takes
   the per-level runtime value on **`misc6.w`**, and a goo room *on* a hell level must get
   both. `areaMult` is the per-profile weight and `mediumTint` the medium's scattering
   colour; **profile *selection* — which room is goo, which level is hell, and both
   constants' per-profile values — is DOOM-0011 §4.5's, not this part's.** This part owns
   only where the resulting density attaches. None of `kAreaDensity`, `areaMult` or
   `mediumTint` exists in the tree today (`rg kAreaDensity linuxdoom-1.10/` → no match);
   L4 declares them.
4. **`wisp` and the dial multiply the whole medium**, so goo and hell billow too and the
   dial thins everything uniformly.

**The profile term is the one addend that is NOT a pure function of `p`, and L4 must not
be left to infer that.** DOOM-0011 §4.5 keys the goo profile on the **primary hit** — "the
room reads goo-foggy when you are looking at or across the goo", blind to goo behind you
or around a corner, an accepted v1 approximation (Q3). So `areaMult` is evaluated **once
per pixel from `FogHit.matFlags`**, not per march sample, and `pathtrace.comp`'s `FogHit`
comment already reserves those fields for exactly this ("the primary-hit fields later
layers (L2-L4) read for height pooling / area profiles"). §3's rule — density at a point
must not depend on what the ray hits — is therefore **relaxed for this addend, in the same
declared way §4.3 relaxes it for the floor layer's `t`**, and INV-9 records the carve-out.
Hell is exempt from the tension entirely: it is a per-*level* flag, so it is constant
across the frame.

**Why the relaxation is tolerable here and was a defect in §3.** The pass-3 defect put
*two different densities in one view* for the same air, because `baseZ` came from each
pixel's own hit. Primary-hit keying does the same thing in principle — and the reason it
is accepted rather than forbidden is that the error is **invisible in the case that
matters**: a goo room's walls, floor and the goo itself all sit inside the profiled room,
so every pixel of that view keys the same way. The visible failure is a doorway edge where
half the screen keys goo and half does not, which is the artefact Q3's per-sector fog
buffer would remove. **Judge it on hardware at L4; do not silently upgrade it to
per-sample, which costs a material lookup per march step.**

**With L4 unshipped the profile sum is empty, and the expression above is then the same
expression as the shipped line** — not an approximation of it. The three surviving factors
differ only in written order, and multiplication is commutative, so the *value* is
identical. **In floating point the reordering is a re-association, which is not
bit-exact — so keep the shipped factor order when L4 edits this line.** §7's
byte-identity acceptance row depends on that, and it is the kind of thing an implementer
tidies without noticing. The shipped line, in `marchFog`:

```glsl
float sigma = (fogDensity(p, baseZ, poolH) + floorFogDensity(p, baseZ, t))
              * strength * skyExposure * wisp(p, rippleTime());
```

**Why this is stated once.** The parent stated it three times — in §4.3b (self-declared
"the single authoritative statement", written before the floor layer existed and so
carrying no floor addend), in §4.3c (structurally right but written before the wisps and
so missing that factor), and in INV-9 (which omitted the strength dial entirely). Each
was right about something the others got wrong, and none matched the shipped shader. The
reconciliation was decided **by the shipped code, not by judgement**; the audit trail is
DOOM-0308 and `DOOM-0011-fix-ledger.md`. The two superseded statements are **deleted
rather than annotated**, because superseded-text-in-place is the failure mode that made
the split necessary.

**`fogStrengthScale`** maps the `;` dial to a multiplier — `level <= 1 → 0.35`,
`level == 2 → 0.65`, else `1.0`. **Every figure in this document is quoted at High
(`1.0`)**, which is where the user judged the look.

### 4.2 The aerial layer

```
σ_aerial(p) = kFogBaseDensity · exp( −max(0, p.z − baseZ) / poolH )
```

— `fogDensity(p, baseZ, poolH)`, in `pt_common.glsl`. `kFogBaseDensity` ships at
**`0.0033`**; the ≈2× raise L1c proposed was **tried and reverted on 2026-07-30**.

**The reference is chosen per sample position — never from the primary hit** (§3's
pass-3 defect):

| sample is | fog sits on | e-fold height |
|---|---|---|
| under open sky | `pc.fogFloorZ` — one altitude for the whole level | `kFogPoolHeight` = **112** |
| under a roof | the floor under the camera, `ro.z − kEyeAboveFloor` | `kFogIndoorPool` = **18** |

`pc.fogFloorZ` is the lowest floor among the level's open-sky sectors, computed once in
`RB_BuildLevelMesh` (`rb_mesh_t::fogFloorZ`) and pushed as a bit-cast float. A real fog
bank has an altitude, not a per-pixel one, so standing on a high ledge you correctly look
down onto its top. Indoors keeps a camera-relative reference because a single global Z
**is** wrong there — interior rooms sit hundreds of units above and below the outdoor
ground — and it is still a per-frame constant, so the invariant holds. Both references
being per-frame constants is exactly what makes **`σ_aerial`** a function of `p` alone.
(`σ` as a whole is not — §3's table lists its two declared relaxations.)

`kFogFloorFallback` was never added and is **not** in the tree.

**Outdoor thickness.** The user asked for fog *"much, much thicker and higher"* outside;
delivered by raising the outdoor e-fold height 18 → **112** with `kFogBaseDensity` left
alone. At High, standing on E1M1's courtyard with the eye **65 units above the fog
altitude** — **the aerial layer alone**, since the floor layer adds its own bounded share
on top (§4.3):

| | 256 u | 512 u | 1024 u | 2048 u |
|---|---|---|---|---|
| ground (ray dips into thicker air) | 44 % | 68 % | 90 % | 99 % |
| wall at eye height (horizontal ray) | 38 % | 61 % | 85 % | 98 % |

*The eye-height row is a closed form and reproduces exactly:*

```
python3 -c "from math import exp; s=0.0033*exp(-65/112); print([round(100*(1-exp(-s*d))) for d in (256,512,1024,2048)])"
→ [38, 61, 85, 98]
```

*The ground row is a numerically integrated dipping ray and is carried from the parent.*
**Ground and wall agree to within a few points at every distance — that agreement *is*
the pass-3 fix**, and the small residual is correct. A far wall at 1024 reads 90 % at its
base, 73 % at +128 and 58 % at +256: a vertical gradient, which is what a bank looks like.

**`kFogPoolHeight` is the outdoor dial.** It is no longer paired with `kFogBaseDensity`:
the sky backdrop's closed form derives its own path length from it geometrically (part 3),
so raising it thickens the horizon too.

### 4.3 The floor layer (L1e / DOOM-0272)

The second term **breaks §3's conflict by not being a medium** — its density falls off
with distance **from the camera**:

```
σ_floor(p,t) = kFloorFogDensity
             · exp( −max(0, p.z − baseZ) / kFloorFogPool )   // hugs the ground
             · exp( −t / kFloorFogRange )                     // and only NEAR you
```

— `floorFogDensity(p, baseZ, t)`. Shipped: `kFloorFogDensity` **0.010**,
`kFloorFogPool` **24.0**, `kFloorFogRange` **256.0**.

**This is deliberately not physical, and that is the point.** Real fog has no idea where
the camera is. But a medium whose *visible contribution is bounded in range* is exactly
what lets mist pool around the player's feet without accumulating into an opaque wall at
distance. It costs nothing to evaluate: `marchFog` already has `t` as its loop variable.

**The one deliberate relaxation of §3's contract.** The height factor still depends on
`p` alone — so "the air at a point cannot depend on what the ray eventually hits" holds —
while the range factor depends on the ray's own parameter, which is a property of the
**view**, not of the world. This is also why it cannot be folded into `fogDensity()`: the
two layers must keep separate e-fold heights.

**`baseZ` is SHARED with the aerial term — the floor layer does not get its own, and every
figure below turns on that.** The shipped call is `floorFogDensity(p, baseZ, t)` with the
same `baseZ` §4.2's table selects. So the layer behaves differently in the two branches,
and the difference is not a tuning artefact but the mechanism:

| at High, eye height | roofed air | outdoors, eye 65 u above `pc.fogFloorZ` |
|---|---|---|
| height above `baseZ` | **41 by construction** (`indoorBase = ro.z − kEyeAboveFloor`) | however high the eye rides — 65 in §4.2's scenario |
| `σ_floor` at the eye | `0.010 · exp(−41/24)` = **0.00181** | `0.010 · exp(−65/24)` = **0.00067** |
| its saturated haze | **37 %** (τ = 0.46) | **16 %** (τ = 0.17) |
| `σ_aerial` at the eye | `0.0033 · exp(−41/18)` = 0.00034 *(`kFogIndoorPool` = 18)* | `0.0033 · exp(−65/112)` = 0.00185 |
| floor ÷ aerial | **5.4×** — the floor layer *dominates* roofed air | **0.36×** — a minority term outdoors |

```
python3 -c "
from math import exp
for lbl,h,pool in (('roofed',41,18),('outdoors',65,112)):
    sf=0.010*exp(-h/24); sa=0.0033*exp(-h/pool)
    print(lbl, round(sf,7), round(sf*256,4), round(100*(1-exp(-sf*256)),1), round(sa,7), round(sf/sa,2))"
→ roofed   0.0018117 0.4638 37.1 0.0003383 5.36
→ outdoors 0.0006665 0.1706 15.7 0.0018470 0.36
```

**Only at the fog altitude itself is the floor layer "3× the aerial layer"** — `0.010`
against `0.0033`, i.e. `p.z == baseZ`, which is the bank's own base and not where the eye
usually is. Quoting that 3× as though it described eye-level air is how the parent came to
compare it against a stale figure of *"the aerial layer's 16 % at 512 units"* — measured
before the outdoor pool rose to 112, and contradicting the parent's own shipped table, which
gives **61 %** there (§4.2). Both errors are corrected here. **Do not confuse that dead
number with the live 16 % in the table above**, which is the *floor* layer's saturated haze
outdoors and is unrelated to it.

**What actually separates the two layers is saturation, not density.** The floor layer's
contribution is **bounded** — it reaches its 37 % (roofed) or 16 % (outdoors) and stops,
because `exp(−t/kFloorFogRange)` cuts it off — while the aerial layer's keeps growing with
distance: 61 % at 512 u, 98 % at 2048 u outdoors at eye height. That bound is the entire
reason the layer exists (§3).

`kFloorFogRange` = 256 is also the middle column of Q26's error table, so the warped march
resolves it to 0.09 % and it cannot band.

**Placement follows §4.4's gate, with its own strengths.** Both layers scale by the same
`skyExposure`, so the floor fog inherits the open-sky test and the seep for free — no
second placement mechanism, no second up-ray. Only the density constants differ:

| | outdoors | roofed air |
|---|---|---|
| aerial layer | `kFogBaseDensity`, `kFogPoolHeight` = 112 | × seeped `skyExposure`, `kFogIndoorPool` = 18 |
| floor fog | `kFloorFogDensity` | `kFloorFogDensity` × seeped `skyExposure` |

The user asked for the outdoor floor fog to be **thicker**; that falls out of
`skyExposure` already, without a fourth constant.

**Part 3 inherits an obligation here, and its size is the OUTDOOR figure.** The sky
backdrop never enters `marchFog`, so its closed form needs a **second addend** for this
layer. Omit it and a horizon-grazing sky pixel disagrees with the wall pixel directly
beneath it — a hard line along the skyline, the exact defect the sky closed form exists to
remove. **A sky pixel is open-sky by definition**, so the disagreement is the outdoor pair
above (**≈0.17 optical depth, ≈16 % haze** with the eye 65 u up), not the roofed 0.46/37 %:
the closed form's `h₀ = max(0, ro.z − pc.fogFloorZ)` is the same reference the outdoor
column uses. It is still far too large to leave on the skyline, and it shrinks as the eye
rises. The derivation, both branches and the cancellation trap near
`|rd.z| = kFloorFogPool / kFloorFogRange` are part 3's (INV-10).

### 4.4 Open-sky exposure — the fog-placement standard

**The standard (user 2026-07-24): *fog lives under open sky.*** Air that can see the sky
carries **full** density; air under a solid roof carries little. This is DOOM-native — an
open-air area is exactly a sector whose ceiling is the sky flat (`ceilingpic ==
skyflatnum`), the same signal `emit_subsector_caps` already uses.

**`skyExposure` gates the SKY-SOURCED haze only — never the area profiles. This is
load-bearing and was got wrong in the first draft.** Goo rooms, hell interiors and
torch-lit dark rooms are all **roofed**, so a formula that multiplied the *whole* product
by `skyExposure` would drive their density to the `kIndoorFogScale` floor — silently
cancelling L4's green goo pool and red hell haze, and making L4's own falsifier ("E3M1
shows haze") fail by construction. The two terms have different **sources** and so take
different gates: outdoor haze comes **from the sky** and must vanish under a roof; goo
outgassing and hell's haze are properties of **the room** and must not.

**What the split does not rescue: the plain roofed room.** A dark, dry, non-hell interior
is the *clear* profile and contributes no `areaMult`, so its air is only the seeped
sky-sourced term. Torch shafts need *something* in that air to light, so
**`kIndoorFogScale` must stay > 0** — Q12's `= 0` option is **struck**. It ships at
**`0.05`**.

**As shipped, per march sample:**

```glsl
vec4  fld     = texture(uSeepField, worldToSeepUV(p.xy));
bool  openSky = fld.g > 0.5;
float seepT       = openSky ? 1.0 : exp(-fld.r / kSeepFalloff);
float skyExposure = openSky ? 1.0 : mix(kIndoorFogScale, kSeepMax, seepT);
```

Both facts the sample needs — *is there sky above it* and *how far is it from outdoor
air* — ride the same field, so this is **one bilinear tap and no rays at all**.

- **The open-sky branch is exactly `1.0`**, on both sides of every amendment. That is why
  the outdoor look the user signed off is preserved by construction rather than by tuning.
- **The indoor branch is a graded seep, not a flat floor.** The user's ask was *"have a
  little bit of the fog come in by open areas exposed to outside."* Shipped:
  `kSeepMax` = **0.9**, `kSeepFalloff` = **384.0**.
- **`seepT` is hoisted deliberately (DOOM-0292).** It has a **second consumer** — the
  ambient sky share, `kIndoorSkyLight` = 0.45 — and evaluating the same `exp()` at two
  sites is how two things silently drift apart under a later edit. It is `1` right at an
  opening, `0` deep inside, and exactly `1` under open sky, so **both** consumers reduce
  to their outdoor form by construction. That second consumer is a **light** floor, not a
  density one: `skyExposure` already bottoms out at 0.05, and multiplying brightness by it
  as well would square the interior's cut and take a deep room to black.

**The DOOM-0281 re-tune, because the shipped numbers are not the spec's first guesses.**
The re-flood mechanism was firing — the play log showed the field going 835 → 715 sealed
cells as walls opened — so the fault was entirely in the two constants, and both were
wrong in the same direction. `kSeepMax` 0.5 put a **2× density step at every threshold**:
air standing in a doorway *is* outdoor air, and capping it at half the outdoor density
means fog visibly halves the instant it crosses the opening, which is the opposite of
seeping through it. `kSeepFalloff` 192 **killed the grade within two door-widths** — 21 %
of outdoor density at 192 units in, 11 % at 384, indistinguishable from a sealed room by
~600 — so a player standing back in a room, which is where players stand, saw nothing.
0.9 leaves a slight lip (a room is still not a courtyard) without the cliff. *(Q16 asked
for exactly this re-judgement and is now closed by it.)*

**The mask must be its own channel, never an epsilon on `d`.** `d = 0` does mean "outdoor
cell", but a **roofed** cell one step inside a doorway also carries a near-zero `d` — the
portal it walks to is seeded at zero. The two are not separable by any epsilon, and an
epsilon that tried would put the full outdoor bank inside the first room behind every
door.

**The up-ray is gone (DOOM-0276), and the reason is the 2-D one.** Until 2026-07-27
`openSky` was a shadow ray fired straight up per sample, and it was the single most
expensive thing in the fog: 24 samples × every fog pixel, **~7.9 ms of an 8.4 ms
feature**. It was 3-D work on a 2-D question — vanilla DOOM is flat-mapped,
`R_PointInSubsector` takes `(x, y)` alone and yields exactly one `ceilingpic`, so "is
there sky above this XY" is a pure function of XY and the field already knows it.

Three differences from the ray, stated so they can be judged on screen:

1. **The roofline moves onto the grid.** Bilinear + a `0.5` threshold puts the boundary
   midway between differing cell centres, so it is accurate to **half a cell** and follows
   the grid rather than the wall. The mist wall at a doorway may sit up to half a cell
   either side of the door. This is the one visible cost.
2. **Height-invariance.** Air under a roof more than 2048 units up used to read *open sky*
   (the ray ran out) and now reads roofed — the field is the more correct of the two here.
3. **The void ring reads roofed** where an unbounded ray would have missed. Free, but only
   after a latent sizing bug had to be fixed — §4.5.

**Superseded and deleted:** the parent's per-sample up-ray mechanism, and the
`RB_MESH_OUTDOOR` per-surface flag it named as a perf fallback. The *granularity* argument
(per sample, not per surface) survives and is what the field preserves; the ray does not.
The flag was never built and is **not** in the tree.

### 4.5 The seep field

At level load, lay a coarse **2-D grid** over the map's XY extent, seed every cell in an
open-sky sector at `d = 0`, then **flood outward through connected open space only** — so
the fill travels through doorways and archways but never through a wall.
`RB_BuildSeepField` (`r_mesh.c`).

**The connectivity test is an OPENING test, not a one-sidedness test.** A step is allowed
only where the linedef is **two-sided *and* the two sectors' openings overlap**:
`min(front.ceilingheight, back.ceilingheight) > max(front.floorheight, back.floorheight)`.
Testing merely "not one-sided" is **wrong** — in vanilla DOOM a **closed door is a
two-sided linedef** whose sector has `ceilingheight == floorheight`, as are windows,
ledges and raised lifts, so the flood would pour straight through every shut door and seep
fog into a door-sealed closet beside a courtyard. This is exactly `P_LineOpening`'s
`openrange > 0`, so **reuse it rather than re-deriving it** — note it returns `void` and
writes file-scope globals (`opentop`/`openbottom`/`openrange`/`lowfloor`), so read the
global and keep the flood **single-threaded**.

**Flood over SEGS, then rasterise — not over grid cells.** A cell-to-cell test cannot
work: two adjacent cells have no single "linedef between them", and comparing the two
cells' *sector heights* says nothing about whether a wall stands between them — a
courtyard and a sealed closet with matching heights pass any height comparison trivially.

**Adjacency comes from SECTORS; the search's nodes are the PORTALS between them.**
Vanilla DOOM has **no minisegs** — `P_LoadSegs` gives every `seg_t` a `linedef` — so two
BSP leaves of the *same* room split by a partition line share **no seg at all**, and a
subsector graph would leave every multi-leaf hall disconnected.

1. **Nodes = portals, not sectors.** The state has to be the *opening*, because step 3
   needs two portals of the **same** sector to carry **different** distances, and a
   sector-indexed Dijkstra can only settle one value per sector — which is exactly the
   flat-per-room result step 3 forbids. A portal is one surviving `seg_t` at its midpoint.
   A seg survives iff it has a `backsector`, its linedef is two-sided, `openrange > 0`,
   **and `linedef->frontsector != linedef->backsector`** — a self-referencing sector (the
   vanilla deep-water / fake-wall trick) is two-sided with a full-height opening but is
   *drawn* solid, so without this test the flood walks straight through it.
2. **Edges join two portals sharing a sector**, weighted by the distance between their
   midpoints. **Seed** every portal at `d = 0` if *either* of its sectors is open-sky, then
   run Dijkstra from the whole seed set at once. Weights are non-negative and the graph is
   finite, so it terminates.
3. **Resolve `d` per GRID CELL, not per node:** `d(cell) = min over the portals of the
   cell's own sector of ( d(portal) + |cell centre − portal| )`, clamped to `dMax`; an
   outdoor cell is `0`. **A per-node value would defeat the whole feature** — `d` would be
   constant across a room, so the seep would step at the room boundary and hold flat
   inside, which is precisely what the user asked to soften.

`R_PointInSubsector` maps a cell centre to its leaf and thence to `->sector`.

**The cell size is 64 units *initially* and doubles until the grid fits — nothing may
assume 64.** `RB_SEEP_CELL0` = `64.0f` (a DOOM flat's own period), `RB_SEEP_MAXDIM` =
`256` texels per side. A map whose XY extent exceeds that **doubles the cell size and
rebuilds**, repeating until it fits. Coarser cells only blur the seep's edge; they cannot
break INV-12, because connectivity was decided on the seg graph *before* rasterisation.

**The degenerate cases, pinned:**

- **A level with no open sky at all** (most hell maps). The seed set is empty, every cell
  gets `dMax`, and the seep collapses to exactly `kIndoorFogScale`. Correct behaviour, not
  a failure: with no outdoors there is nothing to seep in.
- **Unreachable cells** (a sealed room; every cell in void space) take the **finite**
  sentinel `dMax = 8 · kSeepFalloff`. **It must be finite:** a half-float `+inf` multiplied
  by a zero bilinear weight yields `NaN`, which propagates into `σ` and blows the whole
  march. Because `dMax` is *defined* as 8 e-folds, a sealed room stays at the
  `kIndoorFogScale` floor whatever `kSeepFalloff` becomes.
- **The C and GLSL copies must agree.** `RB_SEEP_FALLOFF` / `RB_SEEP_DMAX` (`r_mesh.h`)
  mirror `kSeepFalloff` / `dMax` (`pt_common.glsl`), each carrying a `must match` comment.
  Drift here is a look defect, not a build error — see INV-12's falsifier.

**Sampler state is part of the contract:** `CLAMP_TO_EDGE` on both axes, and **the grid is
padded by one cell of void beyond the map's XY bounding box.** Under `REPEAT` an outdoor
`d = 0` at one map edge would wrap onto indoor air at the opposite edge. The padding is
what makes the clamp safe: the ring lies outside every sector, so it takes the void value,
and `CLAMP_TO_EDGE` extends *that* outward. Without it, a level whose outdoor sector runs
flush to its bounding box would clamp an outdoor `d = 0` out past the map edge — the exact
opposite of the guarantee.

**The ring is only unreachable if the interior cells reach *past* `maxX`/`maxY`.** L1d
sized the grid with a **truncating** divide, which left the last interior centre short of
`maxX` by up to a cell, so real air along the `+X`/`+Y` edges got a bilinear weight on the
void sentinel. Fixed to `ceilf`: `centre(gw−2) = minX + ceil(Δ/cell)·cell ≥ maxX` is what
makes the ring provably unreachable. E1M1's grid went 74×47 → 75×47, so this was live, not
theoretical.

**No march sample leaves the box anyway**, which is the other half of the argument:
`marchFog` is called only on the surface-hit branch — sky pixels take part 3's closed form
and never march — so every sample sits on the segment from the camera to a real geometry
hit, and both endpoints are inside the box.

**Why a 2-D field is sufficient.** Vanilla DOOM has **no room-over-room**: any XY column
belongs to exactly one sector, and `r_mesh.c` emits exactly one floor and one ceiling cap
per subsector, so projecting to XY loses no *topology*. It is deliberately **not** claimed
to be *exact*: `d` is a grid-quantised connected distance, not a true geodesic, and it is
**height-invariant**, so air near the ceiling of a tall hall reads the same `d` as air at
the floor. Sufficient for a soft seep; not a distance oracle.

**Doors are evaluated at their state at flood time, and the field is re-flooded when one
of those `openrange > 0` answers actually flips** (DOOM-0281). So a wall that opens in play
does let the fog in, and INV-12 still holds because connectivity is re-decided from real
openings rather than assumed.

### 4.6 The wisps — the Silent Hill 2 look

**The target (user 2026-07-25, with reference screenshots).** Silent Hill 2 (original PS2,
2001): **near-white, colourless** fog, thick enough that the world fades toward flat grey
at middling range, and — the quality a uniform haze misses — full of **billowing wisps of
visibly varying thickness that drift slowly past**.

**What SH2 actually does**, researched 2026-07-25, with the evidence's limits stated up
front: the `[FOG]` values are reverse-engineered from the **PC port** by an enhancement
project, so its "original" column means *the PC port's* originals and PS2 parity is
assumed. It is three stacked things: hardware **distance fog**; **two fog layers combined
at different densities and alphas**, at least one scrolling; and local swirls plus a
**player-reactive** term. **No layer-2 scroll rate appears anywhere in the block**, so
"two layers scrolling at *different speeds*" is **not** established by it. What is
established — two layers, differing density and alpha, motion on at least one — is enough
to motivate more than one octave, but the two-octave design here is an **analogy, not a
derivation**. The community's "Fog Speed Fix" exists because the PC port scrolled the fog
*too fast* and lost the dread: **slow drift is part of the look.**

*Sources:* [gamedev.net 362970](https://www.gamedev.net/forums/topic/362970-fog-effect-in-silent-hill-2/);
[elishacloud/Silent-Hill-2-Enhancements #246](https://github.com/elishacloud/Silent-Hill-2-Enhancements/issues/246).

**What we take, and what we do not.** SH2 pasted 2-D planes over the screen because a PS2
could not march a volume. We march one, so the faithful translation is to modulate `σ(p)`
with **drifting 3-D noise** — which buys what the original could not have: the wisps have
**depth**, passing in front of *and* behind pillars and monsters and parallaxing correctly
as the camera turns. The player-reactive swirl is **not** taken (DOOM is first-person —
no on-screen body for fog to curl around); split out as **DOOM-0239**.

**As shipped** — `wisp` in `pathtrace.comp`:

```glsl
float a  = uintBitsToFloat(pc.wispAngle);       // per-level heading, DOOM-0300
vec2  cs = vec2(cos(a), sin(a));                // loop-invariant: hoists out of the march
vec3 q1 = p + wispDrift(kWispVel1, cs) * t;  q1.z *= kWispSquashZ;
vec3 q2 = p + wispDrift(kWispVel2, cs) * t;  q2.z *= kWispSquashZ;
float A = 2.0 * texture(uNoiseVol,  kWispFreq1 * q1                 / kWispTexels).r - 1.0;
float B = 2.0 * texture(uNoiseVol, (kWispFreq2 * q2 + kWispOffset2) / kWispTexels).r - 1.0;
float n = clamp((A + kWispWeight2 * B) / (1.0 + kWispWeight2), -1.0, 1.0);
n = n * (1.5 - 0.5 * n * n);                    // odd S-curve — contrast, not amplitude
return 1.0 + kWispAmp * n;
```

Six properties, each of which the parent either mis-stated or never recorded:

- **`kWispAmp` ships at `1.0`**, so density is bounded to `1 ± kWispAmp` = **`0×`..`2×`**
  of base. (The parent said `0.6` / `0.4×`..`1.6×`.) Value noise rarely reaches its
  extremes, so the practical swing is narrower. That swing *is* the "various thickness"
  the user asked for; a ±15 % grain would read as noise, not billows.
- **`kWispAmp = 0` remains an exact no-op**, from the *multiplicative* form
  (`1 + 0·x ≡ 1`) — and the S-curve does not break this, since it only shapes `n`.
- **Feature size is `kWispFreq1 = 1/192`, not the `1/512` first specified.** 512 was tried
  and read as **no structure at all**: a value-noise blob is about one lattice cell across,
  DOOM's rooms run 256–1024 units, so one cell spanned the entire view and the march
  integrated it to a flat wash. 192 puts several blobs across a room and a dozen along a
  sight line. Lower still becomes grain along the ray, which the step count would have to
  pay for.
- **The S-curve is a contrast shaper and is load-bearing.** Trilinear value noise piles
  most of its samples near the mean and a view ray then averages ~10 of them, so the raw
  signal arrives at the pixel as a gentle wash — measured at 6 % peak brightness, which the
  eye reads as "even fog". `n·(1.5 − 0.5n²)` steepens the middle of the range where those
  samples live while pinning ±1. Being **odd**, it leaves the mean at 1, so overall
  thickness is untouched and INV-11 holds.
- **The `clamp` is what makes the S-curve safe.** `f(n) = 1.5n − 0.5n³` has
  `f'(±1) = 0` and **decreases beyond ±1** (`f(1.2) = 0.936 < f(1) = 1`), so without the
  clamp the mapping folds and denser billows would read *thinner* than medium ones. The
  weighted mean of two values in `[−1,1]` is mathematically already in range, so the clamp
  is defensive against filter/precision excursions — but it must stay, because the curve it
  guards is non-monotonic outside that interval.
- **`kWispSquashZ = 2.5` squashes the lattice vertically** — the parent never mentioned it.
  A ray integrates the noise along its whole length, which averages isotropic blobs back
  toward their mean (the reason the first two tunings read as a flat wash). Squashing z
  makes the billows **wide and shallow, like real banks of mist**, so a sight line crosses
  few of them horizontally while the eye still sees layering stacked up a wall. It is the
  cheapest available contrast: no extra tap, no extra step.

**Drift — DOOM-0300, and both halves of it are measurements rather than taste.**

- **Speed.** SH2's fog was captured in PCSX2 (two scenes) and its frame-to-frame change
  measured after a 12 px Gaussian blur so grain could not flatter it: **82–84 % of the life
  in that fog is large-scale billowing**, restructuring completely in **under ~2.2 s**. Ours
  drifted 8 units/s across a 192-unit cell — one cell per 24 s, an order of magnitude too
  slow. `kWispVel1 = 15.0 · vec3(8,3,1)` now gives **‖v₁‖ ≈ 129 units/s, one cell in
  ≈1.5 s**. Free: these are constants inside a lookup already being sampled.
- **Direction: there is none.** The same capture found SH2's fog does **not translate** — a
  ±12 px best-shift search over consecutive frames explained **0 %** of the change in one
  scene and **1 %** in the other. There is no wind; it dissipates and reforms where it
  stands. So **`kWispVel2 = −kWispVel1`** — the two octaves are *exactly* opposed and their
  sum is identically zero. What the eye sees is the **beat** between two counter-moving
  patterns of different frequency, and a beat has no direction to follow.
  *(The parent claimed "the finer octave drifts slower, not merely elsewhere", with
  `v₁ ≈ (8,3,1)` and `v₂ ≈ (−3,4,0.3)`. All three claims are stale: the speeds are now
  equal and opposite by design.)*
- **The per-level heading rotates both octaves by the same angle.** `wispAngle` is seeded
  from `(gameepisode, gamemap)` through an avalanche mix in `RB_BuildLevelMesh`, so it is
  deterministic and a `-shotcompare` golden stays bit-exact. Rotation is linear, so
  `kWispVel2 = −kWispVel1` **survives it** and the net wind stays exactly zero at any angle.
  Rolling the two vectors independently would break the cancellation and reintroduce the
  net wind the reference does not have. Only `xy` turns — a heading on the ground plane, not
  a tilt.
- **No new push lane.** `wispAngle` takes the **second** of the two pad words `misc6`'s
  16-byte alignment had already forced (`fogFloorZ` took the first), so the push range stays
  **240 bytes** (`static_assert(sizeof(RtPushConstants) == 240)`) and `-rtverify`'s 184-byte
  prefix is untouched. **Both pad words are now spent** — the next value genuinely needs a
  `misc7`, and `docs/standards/renderer.md` records that.

**Sampling convention — state it before reading any number.** `noise(u)` is a trilinear
fetch at **texture coordinate `u / kWispTexels`**, wrapped `REPEAT`, where `kWispTexels` =
**64** is the volume's edge. So `u` is in *lattice* units: one texel spans `1/kWispFreq1`
world units and the volume repeats every `kWispTexels / kWispFreq1` world units. **The
velocity sits inside the frequency scale**, so `v` is a plain world-units/second velocity;
writing `noise(p·f + v·t)` instead would drift the field `1/kWispFreq1` = **192×** too
fast. On octave 2 the **whole** argument goes inside the divide — frequency term *and*
`kWispOffset2` — because the offset is part of `u`; divide only the frequency term and
octave 2 shifts by 64× its intended offset.

**`kWispOffset2` = `vec3(17.3, 5.1, 23.7)`** decorrelates the two octaves at `t = 0` and
`p = 0`. Both taps read the *same* volume, so without an offset they are phase-locked at
the world origin and the finer octave contributes nothing there.

**Tiling, and the claim that actually matters (Q21).** Octave 1 repeats every
`64 · 192` = **12288** units, but **the binding period is the finer octave's**:
`kWispTexels / kWispFreq2` = `64 · 192 / 2.5` = **4915 units**.
*(The parent quoted 13107 units, derived from the superseded `1/512`.)*

**Horizontally the no-visible-repeat claim is provable rather than argued from map sizes:**
the march is clamped to `kFogMaxDist` = **2048**, and 2048 < 4915, so no single sight line
can span a period.

**Vertically it is not, and the difference is worth stating rather than glossing.** The
z-squash divides the period by `kWispSquashZ`: `4915 / 2.5` = **1966 units**, which is
*below* `kFogMaxDist`. So the vertical argument still rests on vanilla sector heights
rather than on the march clamp — comfortably, since a 1966-unit floor-to-ceiling span does
not occur in stock DOOM geometry, but it is an assertion about the maps and not a proof
about the march. A ray would in any case have to travel near-vertically for 1966 units to
see it, which the clamp does bound.

```
python3 -c "print(64*192, 64*192/2.5, 64*192/2.5/2.5)"
→ 12288 4915.2 1966.08
```

**Mean-1 does NOT mean "same look" at non-zero amplitude.** Transmittance is
`exp(−∫σ dt)` and in-scatter is weighted by it — both curves — and averaging *through* a
curve is not the curve of the average. So wisped fog reads measurably **thinner** on
average than un-wisped fog of the same base density. Ordinary maths, not a bug, and the
standing rule that follows survives the reverted density raise: **base density is re-tuned
with wisps on**, never inferred from the un-wisped value.

**Octave 2 is finer and fainter:** `kWispFreq2 = 2.5 · kWispFreq1`, `kWispWeight2 = 0.7` —
a **chosen** starting value, not an SH2-derived one. (An earlier draft justified `0.7` as
SH2's `90/128` alpha ratio. Wrong twice: `90` is the Enhanced Edition's *modified* alpha —
the original is `128`, a ratio of `1.0` — and a 2-D compositing alpha is not an octave
weight in a volumetric march.)

**Where the tap lives, and why it cannot move.** In `marchFog` (`pathtrace.comp`), **not**
in `fogDensity` — `pt_common.glsl` is `#include`d verbatim by `bake.comp`, so putting a
sampler there would force the GI bake to declare and bind the noise volume, contradicting
INV-6. **Time comes from `misc6.x`**, DOOM-0183's ripple clock (float seconds from a
`steady_clock` zeroed at first use), already frame-rate-independent and already in the push
block.

**Colour, and how a near-white base coexists with coloured fog.** `kFogColor` ships at
**`(0.55, 0.56, 0.56)`** in linear radiance — brighter *and* colourless, so distance reads
as *pale* rather than merely dim. It replaced the cool blue `SKY_COLOR` at every fog
in-scatter site. It is **not** a global override; it is the **clear profile's** base tone,
and L4's profiles still multiply it:

```
fog colour = sky term:   kFogColor × mediumTint      (L4)
             torch term: emitter Le  (UNTINTED — user decision 2026-08-03)
```

**A torch shaft is NOT tinted by the medium** (user, 2026-08-03). It keeps its emitter's
own `Le`, so a flame reads **warm through green air** and the room's colour comes from the
fog around it. **L4 applies `mediumTint` to the SKY term only.** Consequently: earth-side
maps sit in the clear profile and read SH2 near-white (the default and the majority of
play); hell levels take `kHellTint` for the same wisps lit red/ember; goo rooms take
`kGooTint`; and emitter-lit fog is already coloured by construction, giving a warm core
against a near-white surround — exactly the SH2 street-lamp look.

`kSkyShaftStrength` ships at **0.85** (from 1.0), answering the user's *"it can be slightly
darker though — it is quite bright when outside."* It is the right knob because it scales
in-scatter **brightness** alone; how much the fog *hides* is `kFogBaseDensity`'s job, and
the wisps are a multiplier on density, so lowering density flattens the billows along with
the brightness. The same gain was added to both sky closed forms, which had omitted it — a
no-op only while the gain was 1.0, and a visible seam the moment it moved.

**Keep it from pooling.** SH2 fog is vertically uniform, so height pooling must stay
**gentle** or it will undo this; `kFogPoolHeight` is a look-tune to be judged **with** the
wisps present (Q17).

## 5. Data & resources

**No new resource is introduced by this part.** Both fields already exist and ship.

| Resource | Shape | Notes |
|---|---|---|
| seep field | `VK_FORMAT_R16G16B16A16_SFLOAT`, ≤ `256×256` | `.r`/`.g` this part, `.b`/`.a` part 2. `CLAMP_TO_EDGE`, bilinear. **8 B/texel**, so the worst case is `256·256·8` = **512 KiB** and E1M1's 75×47 is **27.5 KiB**. *(The parent said 256 KB / 14 KB — correct for the `R16G16` era, halved per texel, and stale since DOOM-0289 widened the format.)* |
| noise volume | `64³` single-channel, `REPEAT`, trilinear | **generated at startup, never shipped as an asset** |
| `misc6.x` | ripple/wisp time (bit-cast float) | shared with DOOM-0183 — must stay written unconditionally |
| `misc6.z` | `rb_fog` strength 0..3 | 0 gates the whole march out |
| `misc6.w` | **hell-haze density (bit-cast float) — reserved `0` until L4** | already allocated; L4 needs no new lane |
| `pc.fogFloorZ` | pad word 1 (bit-cast float) | the level's fog altitude |
| `pc.wispAngle` | pad word 2 (bit-cast float) | per-level drift heading |

**Constant inventory, as shipped** (`pt_common.glsl` unless noted):

| Constant | Value | |
|---|---|---|
| `kFogSteps` | 24 | the ≈1.67× raise to 40 was **measured, falsified and reverted** |
| `kFogMaxDist` | 2048.0 | the march clamp |
| `kFogBaseDensity` | 0.0033 | the ≈2× raise was **proposed and reverted** |
| `kFogPoolHeight` / `kFogIndoorPool` | 112.0 / 18.0 | outdoor / roofed e-fold heights |
| `kEyeAboveFloor` | 41.0 | DOOM's `VIEWHEIGHT` |
| `kIndoorFogScale` | 0.05 | **must stay > 0** (Q12's `= 0` struck) |
| `kSeepMax` / `kSeepFalloff` | 0.9 / 384.0 | DOOM-0281 re-tune, from 0.5 / 192 |
| `dMax` | `8 · kSeepFalloff` | finite by necessity — an `inf` yields `NaN` |
| `kIndoorSkyLight` | 0.45 | DOOM-0292; the seep's *second* consumer |
| `kFloorFogDensity` / `Pool` / `Range` | 0.010 / 24.0 / 256.0 | the floor layer |
| `kFogColor` | (0.55, 0.56, 0.56) | linear radiance, near-white |
| `kSkyShaftStrength` | 0.85 | in-scatter brightness only |
| `kWispAmp` / `kWispWeight2` | 1.0 / 0.7 | amplitude bounds density to 0×..2× |
| `kWispFreq1` / `kWispFreq2` | 1/192 / `2.5 ·` that | 1/512 was tried and read as no structure |
| `kWispVel1` / `kWispVel2` | `15·(8,3,1)` / `−kWispVel1` | ‖v‖ ≈ 129 u/s; zero net wind |
| `kWispOffset2` | (17.3, 5.1, 23.7) | decorrelates the octaves at the origin |
| `kWispSquashZ` / `kWispTexels` | 2.5 / 64.0 | wide-and-shallow banks; volume edge |
| `kGooTint` / `kHellTint` | (0.35,0.85,0.30) / (0.90,0.35,0.30) | **declared, unread until L4** |
| `kAreaDensity` | **0.0020 (first guess) — DOES NOT EXIST YET** | goo's profile density; L4 declares it. Hell's equivalent is runtime, on `misc6.w`. Per-profile `areaMult` and `mediumTint` values are DOOM-0011 §4.5's |
| `RB_SEEP_CELL0` / `RB_SEEP_MAXDIM` | 64.0f / 256 | `r_mesh.c`; the cell **doubles** until it fits |
| `RB_SEEP_FALLOFF` / `RB_SEEP_DMAX` | 384.0f / `8×` that | `r_mesh.h`; mirrors of the GLSL pair |

*Inventory enumerated from the declarations, not from this table — an inventory is wrong by
**omission** more often than by a bad entry, so the command must be able to return things
the table lacks:*

```
rg -no '^\s*(const\s+\S+|#define)\s+(kFog\w*|kWisp\w*|kSeep\w*|kFloorFog\w*|kIndoorFog\w*|kIndoorSky\w*|kSkyShaft\w*|kEyeAbove\w*|kGooTint|kHellTint|dMax|RB_SEEP\w*)' linuxdoom-1.10/ \
  | sed 's/.*[[:space:]]//' | sort -u
→ 35 constants
```

**The `const|#define` alternation is load-bearing:** an earlier form wrote
`(const|#define)\s+\S+\s+(k…)`, which requires a type token between the keyword and the
name and therefore **silently skipped every `#define`** — all four `RB_SEEP_*` rows went
unchecked by a command that looked like it checked them.

**31 of the 35 are tabled. The four excluded belong to sibling parts, and are named so the
diff runs in both directions:** `kFogAnisotropy` (the Henyey-Greenstein phase term) and
`kFogLightsPerCell` are part 2's; `kFogSkyDist` (the sky closed form's finite extent) and
`kFogDepthSigma` (the denoiser's depth weight, in `svgf_composite.comp`) are part 3's.

## 6. Performance budget

**The gate is the parent's and it is whole-feature: fog-off → fog-on Δ ≤ 15 % of
present-total at High** (raised from 5 % by the user 2026-07-25, on the reasoning that a PS2
ran this look). **The whole fog currently sits at 0.83 ms of a 14.96 ms megakernel**
(2026-08-02, after DOOM-0296 and the L2b optimisation) — about 5.5 % — so roughly nine
points of the gate are unspent.

**L4's budget, since it is the one unbuilt term this part governs.** The profile addend is
a per-sample multiply-add over a value that is *constant for the frame* (hell) or *constant
for the pixel* (goo, primary-hit-keyed — §4.1), so it needs **no new texture tap, no ray and
no per-sample material lookup**, and it should be at or near free. **Budget: ≤ 0.1 ms added
to the 0.83 ms fog total, measured at High on E1M1 with `RADV_DEBUG=shaderstats` and
`-rtverify -warp 1 1 -noinput`.** A measurement materially above that means the addend was
implemented per-sample rather than per-pixel, which is the mistake §4.1 warns against.

This part's shipped terms are not separately resolvable at 0.83 ms; what the history
establishes is which levers are real:

| Change | Effect | Verdict |
|---|---|---|
| the per-sample up-ray (L1b) | **~7.9 ms of an 8.4 ms** feature | **deleted** — DOOM-0276 replaced it with the field's `.g` channel; fog fell to +4 % of frame time, 31 → 41 FPS |
| `kFogSteps` 24 → 40 | MAE 0.153/255 at Low, 2.86 at High — against **2.41** for the same build vs its own second run | **reverted**: at or under the engine's own noise floor |
| `kFogBaseDensity` ≈2× | moves the sky's aerial perspective most (see below) | **reverted** |
| the L1c wisps | two taps per sample | shipped; the tap is the cost, and the S-curve and squash are free |
| the seep field | **one bilinear tap, no rays** | shipped. Load-time build **target** ≤ 20 ms on E1M1 — a target, never measured and with no §11 row; the in-play re-flood (DOOM-0281) has no budget stated either, and part 2's clearance re-bake measured 4.1 ms / 2.9 ms against a ≤ 6 ms gate, which is the nearest comparable |

**Registers and occupancy are NOT levers on this megakernel** — 96 VGPR, 0 spills and 8
waves-per-SIMD are identical with fog on and off; the 8-wave ceiling is RADV's ray-query
LDS stack, not VGPRs. Fog cost is plain instruction issue. Measure with
`RADV_DEBUG=shaderstats` plus `-rtverify` (which needs `-warp 1 1` to reach a level), and
always pass `-noinput`.

**A saving here can be larger at Low than at High**, because thin fog never trips the
`trans < 0.003` early-out. Quote a figure with its strength level or it is not a figure.

**Why a density raise is not a free brightness knob.** The sky backdrop's closed form uses
the *same* `kFogBaseDensity`, so doubling density **squares** transmittance everywhere —
costing most where optical depth is already high, i.e. on the sky. At High from E1M1's
courtyard, haze by elevation above the horizon runs 98 % / 91 % / 70 % / 45 % / 34 % at
1° / 5° / 10° / 20° / 30°; a density raise lifts every one of those. There is no
"halve `kFogSkyDist` to cancel it" trick, because that clamp only bites within a couple of
degrees of the horizon. The honest levers are `kFogPoolHeight` or a sky-only density.

## 7. Build order

Everything below is **shipped and user-signed-off** except L4.

| Task | State |
|---|---|
| **L1** uniform haze skeleton | ✅ shipped (`84e8b35`, `e7753b3`) |
| **L1b** open-sky exposure + sky backdrop | ✅ shipped (`1345c92`), signed off — *"outside and not inside"* |
| **L3 pooling** (pulled forward ahead of L2) | ✅ shipped — three passes; §3's per-sample reference is the fix |
| **L1c** wisps + near-white `kFogColor` | ✅ shipped; the density and step-count raises it proposed were reverted |
| **L1d** the seep field | ✅ shipped, re-tuned by DOOM-0281 |
| **L1e** the floor layer | ✅ shipped (DOOM-0272), signed off |
| **DOOM-0276** up-ray → field lookup | ✅ shipped (`1815fe1`) — fog +35 % → +4 % |
| **DOOM-0281** re-flood on a real opening flip | ✅ shipped (`e868b29` + re-tune `8b41786`) |
| **DOOM-0292** `kIndoorSkyLight` | ✅ shipped |
| **DOOM-0300** 15× drift speed + per-level heading | ✅ shipped — **the speed owes a user look judgement** (§10) |
| **L4** area profiles — goo / hell | 📋 **not built.** §4.1 pins the term's placement; §4.4 pins the gate; the tint decision is pinned above |

**L4's acceptance rows**, since this part owns the density term it adds:

- a goo room shows green fog **under a roof** — fails by construction if `skyExposure` ever
  multiplies `areaMult`;
- **E3M1 shows haze** with no open sky anywhere in the level;
- a torch shaft inside a goo room reads **warm, not warm-through-green** (the 2026-08-03
  tint decision);
- **byte-identity, stated so it can actually pass:** on a level with **no goo profile in
  view** *and* `misc6.w = 0`, the frame is byte-identical to today. **Not "`misc6.w = 0`"
  alone** — goo's density is the compile-time `kAreaDensity`, not that lane, so a nukage
  room gains its addend regardless of what `misc6.w` holds. A check written the shorter way
  fails on the correct build;
- **≤ 0.1 ms** added to the fog's 0.83 ms (§6).

## 8. Invariants

**Ids are inherited from the parent and never renumbered.**

- **INV-9** — **density composition + the open-sky standard.** Fog density is exactly
  §4.1's σ and nothing else. `skyExposure = 1` under open sky and grades from `kSeepMax` to
  `kIndoorFogScale` under a roof; **it gates the sky-sourced addends ONLY, never
  `areaMult`.** The aerial and floor layers are **separate addends on the gated side**:
  fold the floor term into `areaMult` and it stops clearing under a roof; fold it into the
  aerial term and it inherits `kFogPoolHeight`, the one thing it must not share. Exposure
  is measured **per march sample**, read from the field's mask channel — never traced, and
  never as an epsilon on `d`.
  *What breaks it:* the profile sum moved **inside** the `skyExposure` gate — a sealed goo
  room is then driven to `kIndoorFogScale` (5 %) and L4's "E3M1 shows haze" falsifier fails
  by construction; a factor that multiplies only **part** of the sum rather than the whole
  of it; a sky-sourced addend placed outside the gate; the two sky-sourced layers merged
  into one term; `kIndoorFogScale` set to 0 (torch shafts lose the medium they light); and
  the aerial layer's `baseZ` or `poolH` made hit-dependent (§3's pass-3 defect). *Note a
  plain extra multiplicative factor is NOT a breach — the factors reorder freely, which is
  why the falsifiers are about **placement relative to the gate and the sum**, not order.*
  *Test:* `rg -n 'float sigma =' -A1 linuxdoom-1.10/shaders/pathtrace.comp` must show the
  two addends, the gate, the dial and `wisp` — and, once L4 lands, the profile sum outside
  the gate. Plus L4's "E3M1 shows haze" row.

- **INV-11** — **wisps.** Density is modulated by **two octaves of drifting 3-D value noise**
  from a single **startup-generated** volume, multiplying the whole medium so goo and hell
  billow too. **`kWispAmp = 0` is an exact no-op**, from the multiplicative form rather
  than from mean-1 — and the S-curve preserves this because it only shapes `n`. Mean-1
  does **not** preserve the look at non-zero amplitude, so base density is re-tuned with
  wisps **on**. The drift has **zero net wind** (`kWispVel2 = −kWispVel1`), which survives
  the per-level rotation because rotation is linear. Drift time is `misc6.x`, so **no new
  push lane**.
  *What breaks it:* rolling the two velocity vectors independently (reintroduces the net
  wind the reference measurably does not have); dropping the `clamp` before the S-curve
  (the cubic folds beyond ±1 and dense billows read thinner than medium ones); making the
  shaper even rather than odd (shifts the mean and silently re-thickens the fog); moving
  the tap into `pt_common.glsl` (drags the noise volume into `bake.comp`, breaking INV-6);
  shipping the volume as an asset.
  *Test:* `kWispAmp = 0` renders byte-identical to **L1b plus L1c's colour swap** — the
  comparison build has to be named, because L1c also replaced `SKY_COLOR` with `kFogColor`
  at every in-scatter site, so a genuinely *pre*-L1c build differs in colour no matter what
  else is held. Hold `kFogColor`, `kFogBaseDensity` and `kFogSteps` at their **shipped**
  values on both sides. And by diff: no noise file enters the repo or a WAD, and
  `bake.comp` declares no noise sampler
  (`rg -c uNoiseVol linuxdoom-1.10/shaders/bake.comp` → 0).

- **INV-12** — **the seep field.** The field is flood-filled **through connected open space
  only**, never straight-line, so fog can **never** reach a sealed room that merely shares
  a wall with an outdoor area — such a room keeps the `kIndoorFogScale` floor (it is *not*
  fog-free; the floor is nonzero by design). Connectivity is an **opening** test
  (two-sided **and** `min(ceilings) > max(floors)`), **not** a one-sidedness test. Three
  further conditions are part of the guarantee, not detail: a portal with
  `frontsector == backsector` is **excluded**; the grid is **padded by one cell of void**
  beyond the map's XY box, with the interior cells sized by `ceilf` so they reach past
  `maxX`/`maxY`; and the sentinel `dMax` is **finite**. It is rebuilt per level, re-flooded
  when a real opening flips, read with a **single bilinear tap**, and adds **no rays**.
  *What breaks it:* the weaker one-sidedness test (a closed DOOM door is two-sided — fog
  leaks into every door-sealed closet); the self-referencing-sector portal (the same leak
  in another costume); a truncating grid divide (real edge air samples the void sentinel);
  `REPEAT` sampling or dropping the pad ring (an outdoor `d = 0` wraps or clamps onto
  indoor air); an infinite sentinel (`NaN` through the whole march); and **drift between
  `RB_SEEP_FALLOFF`/`RB_SEEP_DMAX` and `kSeepFalloff`/`dMax`**, which is a look defect
  rather than a build error.
  *Test:* a sealed room sharing a wall with outdoors is visually indistinguishable from its
  pre-L1d self. For the mirrored constants:
  `rg -n 'RB_SEEP_FALLOFF|RB_SEEP_DMAX' linuxdoom-1.10/r_mesh.h` against
  `rg -n 'kSeepFalloff|float dMax' linuxdoom-1.10/shaders/pt_common.glsl` — 384 and 8× on
  both sides.

## 9. Alternatives considered

- **A single density layer, tuned harder.** Rejected structurally, not on taste: §3's
  conflict is inside one term and every re-balance on 2026-07-27 traded one want for the
  other.
- **Extra sky-visibility rays per sample for the indoor grade.** No load-time work and
  correct in full 3-D, but it multiplies the march's ray cost and sparkles at half-res —
  and DOOM-0276 then deleted the *one* up-ray that did ship for exactly that reason.
- **The per-room `RB_MESH_OUTDOOR` flag as the indoor grade.** Free, but fog would step
  abruptly at the room boundary rather than drifting in — precisely the behaviour the user
  asked to soften. (The same flag was also offered as the up-ray's perf fallback, a
  *different* role; neither shipped and the flag is not in the tree.)
- **A per-cell `d` threshold as the open-sky mask.** Rejected: not separable by any
  epsilon (§4.4).
- **Raising `kFogSteps` to resolve the floor layer.** Falsified before any shader was
  written: error scales as `dt/R`, so 64 uniform steps still band at a 128-unit range at
  2.7× the cost. Redistributing the same 24 samples fixes it outright (Q26).
- **A 3-D seep field.** Unnecessary — vanilla DOOM has no room-over-room, so XY loses no
  topology (§4.5).

## 10. Open questions

| id | Question | State |
|---|---|---|
| **Q3** | **Two halves, both this part's.** (a) The indoor floor reference is one value per pixel (the camera's floor), not per pocket. (b) The profile density is **primary-hit-keyed** rather than read from a per-sector fog volume — so goo behind you or around a corner does not fog the air (§4.1) | both accepted as v1 approximations; the per-sector fog buffer is the deferred honest alternative |
| **Q7** | L4's goo/hell **densities** and the hell-detection thresholds — all first guesses | **open — L4's other tuning task**, with Q20. *(Distinct from DOOM-0183's own Q7, which `pathtrace.comp` cites on `kWetSheenStrength`; Q-ids are not unique across specs either)* |
| **Q9** | `kFogColor` is defined in **linear**, but the sky branch writes a display-encoded colour — does the same numeric triple transfer? | **open**, part 3's to close |
| **Q12** | `kIndoorFogScale`'s value | **closed** — `= 0` struck; ships 0.05 |
| **Q16** | Is `kSeepMax` too low against the shipped indoor pool? | **closed** by DOOM-0281 — 0.5 → 0.9, falloff 192 → 384 |
| **Q17** | Judge `kFogPoolHeight` *with* the wisps present, not before | open, a look tune |
| **Q19** | `d` is grid-quantised and height-invariant, not a geodesic | accepted |
| **Q20** | `kHellTint`/`kGooTint` were picked against a **blue-grey** base and must be re-judged against the near-white one | **open — L4's first tuning task** |
| **Q21** | Does the noise volume tile visibly? | **closed horizontally** — binding period 4915 u > `kFogMaxDist` 2048, so never within one sight line. The **vertical** period is 1966 u (the z-squash), below the clamp, so that axis rests on vanilla ceiling heights rather than on a proof (§4.6) |
| **Q24** | Does a `kFogBaseDensity` raise cost the mountains? | **closed** — yes, and the raise was reverted; §6 records the coupling for any future move |
| **Q24a** | `kFogSkyDist` as the layer's finite horizontal extent | closed, shipped |
| **Q25** | Does the floor fog need its own up-ray, or does sharing `skyExposure` suffice? | **open** — sharing is what shipped; the risk is that the floor fog is densest exactly where the aerial layer is thinnest |
| **Q26** | Step count vs sample warp | **closed** before any shader was written — warp `t = tMax·s²`, exponent 2 |
| **Q31** | **Is DOOM-0300's 15× drift speed right on hardware?** The measurement says SH2 restructures in ~2.2 s and ours now does ~1.5 s per cell, but nobody has judged it in play | **open — owed by the user, a genuine look call** |

*(Q-ids are permanent because the shaders cite them. Verified sites: `Q9`, `Q12`, `Q24a`
and `Q26` appear in `pt_common.glsl`; `Q26` also in `pathtrace.comp`. **`Q21` is cited from
no source file** — an earlier draft claimed it was. Q31 is new here and collides with
nothing in `linuxdoom-1.10/`.)*

```
rg -no 'Q[0-9]+[a-z]?' linuxdoom-1.10/shaders/ | sed 's/.*://' | sort -u
→ Q1 Q12 Q14 Q2 Q24a Q24b Q26 Q4 Q6 Q7 Q8 Q9        (no Q21)
```

## 11. What checks this

| Claim | Checked by |
|---|---|
| σ has the shipped shape | INV-9's grep; the megakernel would not compile if a symbol were wrong |
| `kWispAmp = 0` is the identity | INV-11's byte-identity run |
| the fog look overall | `-shotcompare` golden, re-blessed with fog at its shipped default |
| no sealed-room leak | INV-12's acceptance row — visual, no automated gate |
| the C/GLSL constant mirrors agree | INV-12's paired grep |
| every constant in §5 | the `rg` inventory line under that table — which enumerates **from the source**, so it can return entries the table lacks |
| the aerial haze figures | the `python3` line in §4.2 — **the eye-height row only**; the ground row is a numerically integrated dipping ray carried from the parent and is **unchecked** |
| the floor-layer figures, both branches | the `python3` line in §4.3 |
| the tiling periods | the `python3` line in §4.6 (horizontal bound proved against `kFogMaxDist`; the vertical one rests on vanilla ceiling heights) |
| the Q-id citation sites | the `rg` line under §10 |
| invariant ownership is 1:1 | the `rg -o 'INV-[0-9]+'` recount under §2's table |
| the seep field's build cost | **nothing** — §6's ≤ 20 ms is a target that was never measured, and the in-play re-flood has no budget at all |
| RT path unaffected structurally | `-rtverify` (needs `-warp 1 1`) |
| **that the σ statement stays singular** | **nothing.** No mechanical check forbids a second density formula being written into a sibling part. This is the failure that produced DOOM-0308, and the only guard is that all three parts cite §4.1 rather than restating it |
| **that the seep field's channel ownership is respected** | **nothing.** Part 2 could re-describe the build; only §2's table says it must not |
| **the look of L4's tints against the near-white base** | **nothing** — Q20, a human judgement |
| **the drift speed** | **nothing** — Q31, a human judgement |

**Five `nothing` rows** — one more than the first draft had, because the seep field's build
cost turned out to be a target wearing a measurement's clothes. Two are look judgements
that no gate can hold (Q20, Q31). Two are document-level and are exactly what the split
created: three parts that must not re-diverge. One is an unmeasured cost.

**The σ row has already been cashed once, which is the argument for mechanising it.** The
first draft of this document claimed there was "no second σ statement anywhere in the
split" — and the parent's §4.5 was carrying a partial one at that moment, omitting the floor
addend. If a further σ statement appears, the mechanical answer is a `/doc-lint` check that
greps every part of the split for `σ(` and `sigma =` outside this file's §4.1.
