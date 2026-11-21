# DOOM-0170 — cold-eyes loop log

Review record for `docs/specs/DOOM-0170-raster-performance-mode.md`.

## Cold-eyes loop log

- **2026-07-09 — DOOM-0170 initial review (4 cold passes; 2 lanes each — accuracy/
  conflicts + consistency/implementability).**
  - **Loop 1** (0 CRITICAL): fixed the muzzle-flash-is-an-emitter error (it rides
    `extralight`, not `emitBuf` — verified against `BuildDynamicEmitters`); corrected
    the emitter record to a triangle *area* light (`v0 v1 v2 Le cdf pdf`, no
    pos/intensity/radius); specified the nearest-N cull as a CPU per-subsector list
    (not an in-shader sort over 100+ emitters); split Pass B into DIRECT/INDIRECT so
    AO modulates the indirect term only (§4.3 was unbuildable against one combined
    target); reconciled the DOOM-0169 raster-skip conflict (raster now reads the
    probes, so the bake must run); pinned the reference scale (INV-2 was
    unfalsifiable); gave blob shadows a pass slot (B′); added the §6 constants table
    + a TOC; retitled §4.2 (mechanism, not "resolves").
  - **Loop 2** (0 CRITICAL): loop-1 fixes held (not re-raised). Fixed the `DOOM-0145`
    mis-citation (a Windows bug, not the Render Scale feature — repointed to
    `render_scale`/`m_misc.c`); the `pcData[16]`/`mesh.vert` attribution (the shader
    field is `pc.extralight`; `pcData[16]` is the `r_vulkan.cpp` upload); `DOOM-0009`
    "shipped" → in-progress (roadmap 🚧); budgeted the new per-frame CPU cull (≤ 1 ms)
    + added the per-pass ms table; made INV-3 numeric (1 %-low ≥ 0.6× mean, no frame
    > 24.9 ms); "test scale" → "reference scale".
  - **Loop 3** (0 CRITICAL/HIGH): fixed the bake-vs-INV-3 contradiction (`RunGiBake`
    is synchronous — bake eager at load under the load screen, explicitly excluded
    from INV-3's steady-state bar); deferred the undefined "sun" dominant light (DOOM
    has no sun in map data — flashlight-only for now, §9 Q2); added the
    sprite/billboard draw to the §3 pipeline (Pass A/B′ depend on it); reworded the
    DOOM-0010 provenance (extends, not "is its RT-off portion"); added a **reciprocal
    supersession pointer to DOOM-0009 §2**; retagged Q1 (L2→L1).
  - **Loop 4** (0 CRITICAL/HIGH) — **polish-converged.** Only citation-label / wording
    nits left: `RunGiBake`-synchronous cite §4.3→§4.1; removed a vestigial
    mid-session-bake sentence; "sprite pass"→"Pass-B billboard draw"; deduped the
    extralight mechanism to §4.1; reuse the stored emitter power weight
    (reuse-before-rewrite); pinned the NORMAL+SHINE format; split two long sentences.
    Stopped per the polish-convergence rule.
  - **Verified against disk each loop:** emitter record layout
    (`BuildStaticEmitterSet`/`BuildDynamicEmitters`), muzzle flash rides
    `extralight`/`pcData[16]`, `RunGiBake` synchronous, `RB_ApplyTierRt` 0/6,
    `render_scale` (`m_misc.c`), and every cited roadmap status.
- **impl 2026-10-22 (DOOM-0482, no reviewer dispatched)** — Amendment, not a review loop. §4.1: each raster light record gains a reach (7 floats); pickups (MF_SPECIAL) carry PICKUP_LIGHT_REACH and fade to nothing there, every other light carries 0. §6 gains the dial. Records the user's decision of 2026-10-22, built and verified in the same commit (E1M2 Solid captures).
- **impl 2026-11-18 (DOOM-0630, no reviewer dispatched)** — Amendment, not a review loop. Settles §9 Q1 toward horror at the user's direction of 2026-11-17. §4.1: the base shade is `horrorSectorShade(light) × horrorDistanceFade(light, dist) × BASE_SECTOR_DIM` from `shaders/formulas/sector_light.glsl`, shared with the traced view's ambient floor; a room light below `RASTER_LE_FULL` lights in proportion to its Le; `RADIUS` 512 → 256. §6 gains the dials. Captured at five spots in Classic, Solid and Ultra (both views); the user approved the sheets.
- **impl 2026-11-21 (DOOM-0645, no reviewer dispatched)** — Amendment, not a review loop. §4.1: a light Thing that is not a pickup carries `reach = -LAMP_POOL_REACH` and adds a close pool with its own colour-keeping roll-off on top of the room light; a reach of 0 now means a wall or flat light. §6 gains `LAMP_POOL_REACH` and `LAMP_POOL_STRENGTH`. Records the user's request of 2026-11-21, built and verified in the same commit (E1M1 lamp captures in Solid and Ultra, both views).
