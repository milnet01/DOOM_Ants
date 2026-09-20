# DOOM_Ants optimisation & refactor pass — findings
# 2026-09-20. 14 cold lanes, questions O1 efficiency / O2 duplication /
# O3 simplification / O4 dead code. Lanes report; they do not fix.

## Lane 1 — r_vulkan.cpp core (device, swapchain, accel structures, compute pipelines)

- [O2] HIGH r_vulkan.cpp ~2631-3355 — eleven compute pipelines built by nine
  hand-written near-copies of one six-step sequence (~490 lines). Descriptor
  pool sizes derived by hand at every site; a comment at the bloom site records
  someone already having been bitten by that. CreateSvgfPipelines has ALREADY
  factored it into a table-driven loop — the shape is written once correctly
  and retyped seven more times around it.
  Change: CreateComputeSet(binds,n,setCount,...) + CreateComputePipeline(sets,
  setCount,pushBytes,spv,spvLen,spec,...). Pool sizes derived by summing
  count*setCount per type so the trap cannot recur. Effort LARGER.

- [O2] HIGH r_vulkan.cpp ~2037-2565 — the triangle-geometry descriptor written
  out five times; the TLAS instances block twice, field-identical. A comment
  ("DOOM-0163: NON-opaque, must match the build") holds an invariant across two
  functions 400 lines apart, by prose alone.
  Change: TriGeom(verts,vertCount,flags), InstGeom(instBuf), CreateAs(...).
  Make the world BLAS flags a named constant both the build and the refit pass.
  Effort LARGER.

- [O1] MEDIUM r_vulkan.cpp ~2170-2372 — six buffers and two acceleration
  structures whose sizes are COMPILE-TIME CONSTANTS (4096*6 verts,
  kMaxTlasInstances=3) are destroyed and rebuilt on every level load.
  Change: split DestroyAccelerationStructures into DestroyLevelAS (per level)
  and DestroyStaticRtStorage (shutdown only); guard the constant-size creates
  on a null handle. Effort LARGER. Level-load hitch, NOT a frame-time win.

- [O1] MEDIUM r_vulkan.cpp ~2106-2383 — four one-time submits per level load,
  each a full vkQueueWaitIdle, where two suffice. Only one gap is forced (the
  host must read the compacted size). Same shape DOOM-0131 removed from the
  refit path. Effort QUICK, but land after the CreateAs helper or they collide.

- [O2] MEDIUM r_vulkan.cpp ~199-224 vs ~1434-1553 — the capability gate that
  decides which tiers the menu offers is written TWICE (RT extension scan, and
  the four-term bindless feature test), and the two copies are required to
  agree by comment alone. If they part company the menu offers a tier the
  device then I_Errors on, after the Classic window is gone.
  Change: hoist DeviceHasRT and a new DeviceHasBindless above RB_VulkanProbe;
  call from both. Also: PickPhysicalAndDevice calls DeviceHasRT twice on the
  same device, re-enumerating extensions. Effort QUICK.

- [O1] LOW r_vulkan.cpp ~1839 — FindMemoryType re-queries the device memory
  properties on every allocation (28 call sites); the result is immutable for
  the life of the process. Cache it at the end of PickPhysicalAndDevice.
  Effort QUICK. Init/level-load only, not a frame win.

- [O1] LOW r_vulkan.cpp ~2434-2560 — device addresses of buffers that never
  move are re-queried every traced frame (six calls). Change: optional outAddr
  on CreateRtBuffer. CAVEAT: g.vbuf is re-pointed per frame slot by DOOM-0074,
  so its address must be cached PER SLOT, never as one scalar. Same for
  spriteVbuf and lightBuf. Effort QUICK.

- [O4] LOW r_vulkan.cpp ~2539 — a per-frame store into an AS instance slot the
  build never reads (mask=0 written to a slot outside the built range).
  Delete the else branch, or keep it and fix the comment. Effort QUICK.

## Lane 2 — r_vulkan.cpp render targets, render pass, descriptors, CreatePipeline

- [O2] HIGH r_vulkan.cpp ~3387 — the SVGF 10-binding image set written by hand
  THREE times; the third copy has already been hand-patched to keep up (the
  same "b9 DOOM-0011 L1 fog target" comment appears twice). The correct shape
  is in the same function: the split set is derived by copying the writes and
  re-pointing one binding, with a comment saying why.
  Change: WriteGbufferSet(ds, outColor) called three times. Effort QUICK.

- [O2] HIGH r_vulkan.cpp ~3529 — image+memory+view creation hand-rolled twelve
  times. CreateRtBuffer is exactly this helper for BUFFERS with ~20 call sites;
  there is no image twin. CreateSceneTarget already gropes toward it by copying
  structs. ~250-300 duplicated lines in this slice alone; sites have already
  drifted (only some set sharingMode).
  Change: CreateImage2D(ext,fmt,usage,aspect,...). Leave the two atlas paths
  alone (they sub-allocate many images from one allocation, deliberately).
  Effort LARGER.

- [O3] MEDIUM r_vulkan.cpp ~5578 — CreatePipeline builds nine pipelines by
  mutating ONE shared create-info and undoing the mutation, including two dead
  "restore (defensive)" stores nothing reads. Two blocks in the SAME function
  already show the form that needs no undoing (svPci/blPci copies). This defect
  class has already shipped once in this file (the CreateRenderPass dep
  mutation, whose comment records it).
  Change: per-pipeline create-info copies; delete the five restores; add a
  two-line MakeStages(vert,frag) helper collapsing six repeats. Effort QUICK.

- [O1] MEDIUM r_vulkan.cpp ~3550 — the resize path drains the whole queue FOUR
  separate times to park images in a layout, each park block hand-rolled.
  Change: ParkImages(cb,imgs,n,aspect,layout,stage,access) + thread one command
  buffer through the resize path. Effort LARGER (changes signatures of
  CreateSvgfTargets/CreateTaauTargets). Resize hitch, NOT a frame-time fix.

- [O2] LOW r_vulkan.cpp ~4131 — the seep field is packed and staged into
  host-visible memory TWICE per level load: once into a heap vector plus a
  persistent staging buffer, then again by CreateSampledImage's own staging.
  RecordSeepRefresh already demonstrates the one-buffer path. Effort QUICK.

- [O1] LOW r_vulkan.cpp ~4436 — during a fog roll-in the seep ease and repack
  walk every cell every frame, re-converting three channels that cannot have
  changed. INFERRED, unmeasured; E1M1's grid is small, worst case is 256x256.
  MEASURE with the CPU profiler during a door cycle before touching it.

## Lane 3 — r_vulkan.cpp atlas, HD materials, text and menu resources

- [O2] HIGH r_vulkan.cpp ~7121 — UploadAtlas and BuildHdSet EACH write out the
  same batched bindless-image-array upload longhand (~100 lines apiece): the
  hand-rolled memory sub-allocator with its offset walk, the staging offset
  table, the barrier pair, the view loop. The code says VMA "does this properly
  in a later increment" — so the provisional allocator exists twice. A fix made
  in one copy and not the other is a GPU fault or corruption in ONE TIER ONLY.
  Effort LARGER. Highest-consequence duplication found in the Vulkan back-end.

- [O2] HIGH r_vulkan.cpp ~6569 — the menu cursor pipeline is an 87-line VERBATIM
  copy of the text pipeline, differing only in the fragment shader module. The
  code's own comment says so. Change: MakeTextPipeline(frag) called twice.
  Effort QUICK. This is what makes CreateTextResources 404 lines.

- [O2] HIGH r_vulkan.cpp ~7352 — the liquid-flat name table exists TWICE
  (ForceLiquidEmissive and FlagLiquidFlats: same seven names, same lookup), and
  the first copy's own comment says a second copy must not exist ("a second
  table would be a second answer waiting to disagree"). Effort QUICK.

- [O3] MEDIUM r_vulkan.cpp ~6314 — CreateTextResources is FOUR resources with
  four independent failure policies in one 404-line function; the author already
  marked the boundaries with a bare { } scope and a hand-written flag gate.
  Change: CreateTextPipeline / CreateMenuCursor / CreateMenuLogo. Effort QUICK.

- [O1] MEDIUM r_vulkan.cpp ~7411 — EnsureHdMaterials resolves every CSV row's
  DOOM name TWICE through the same resolver; the second loop exists only to
  recover an id the first computed and discarded. Invisible at ~10 hero rows;
  the commented design target is a full-WAD sidecar. Effort LARGER (crosses
  rb_materials.h and its test).

- [O1] MEDIUM r_vulkan.cpp ~7489 — HD upload-list assembly is O(kept x all
  decoded), scanning the whole decoded vector twice per material. The code
  immediately below states the intended scale (~2000 maps), at which this is
  ~40M iterations per level load. Change: bucket by id in one pass.
  Effort QUICK. INFERRED.

- [O2] MEDIUM r_vulkan.cpp ~5853 — six hand-rolled create/query/allocate/bind
  buffer sequences where CreateRtBuffer already does exactly that. ~90 lines.
  Effort QUICK, mechanical.

- [O1] MEDIUM r_vulkan.cpp ~7438 — every HD map is fully decoded and downscaled
  BEFORE the budget pass decides which materials to drop; dropped ones' pixels
  are then freed unused. Zero cost today (nothing is over budget); becomes a
  level-load stall exactly when the budget starts doing its job.
  Change: rb_image_info (header-only) for the budget pass. Effort LARGER.

- [O2] LOW r_vulkan.cpp ~6867 — cursor and logo sprite paths are the same six
  functions written twice (~45 lines). Rule of Three already met. Effort QUICK.

- [O2] LOW r_vulkan.cpp ~7516 — grime and dirt global-overlay loads are two
  near-copies that have ALREADY DRIFTED: the grunge block resets its index and
  the dirt block does not. Effort QUICK.

- [O1] LOW r_vulkan.cpp ~6788 — rb_text_draw walks each string and its glyph
  table twice per call (shadow pass + main pass) on the per-frame menu path.
  Change: one pass writing into two vectors, preserving all-shadows-under-all-
  glyphs order. Effort QUICK.

- [O3] LOW r_vulkan.cpp ~6809 — rb_menu_safe_bottom's one-shot diagnostic can
  only ever report the uninteresting case (first call is the title screen).
  Effort QUICK.

## Lane 5 — r_vulkan.cpp per-frame hot path (RecordRtTrace, Present, shutdown)

- [O3] HIGH r_vulkan.cpp ~10266-11360 — RB_Vulkan_Present is 1,095 lines, and
  its phases are ALREADY NAMED by the GPU profiler's six timestamp slots
  (shadow, scene, SSAO, bloom, composite, HUD); they just are not functions.
  The raster branch is an un-indented `else {` whose close needs a comment to
  be findable. Three comment blocks inside it exist purely to tell a reader
  which of two far-apart sites they are looking at.
  Change: extract in frame order as file-static functions — AdvanceFrameSlot,
  ReadbackGpuTimers, ServiceRtVerifyGate, ArmShotCapture, RecordRasterFrame,
  RecordDevShot, SubmitAndPresent, WriteDevShotPng, RunShotGateAndExit,
  ReportCpuProfile; then RecordRasterFrame splits along its own timestamp slots.
  State is already global, so no parameter is invented. LEAVE the build-ahead
  scheduling inline — its whole meaning is its position relative to the fence.
  Effort LARGER. Command stream must come out byte-identical.

- [O3] HIGH r_vulkan.cpp ~9245-9969 — RecordRtTrace is 725 lines, of which 140
  are a push-constant fill containing NO Vulkan call at all. The struct is
  declared inside the function, which is what prevents the fill being anywhere
  else. renderer.md says "there is no free lane" and tells the reader to grep
  usage sites — a rule that exists because the allocation is invisible inside a
  700-line function.
  Change: hoist RtPushConstants and SvgfPC (with their static_asserts) to file
  scope; extract RtFrameGeometry, RecordAsUpdates, FillRtPushConstants,
  RecordMegakernel, RecordSvgfChain, RecordModeLabel, RecordFinalBlit,
  AdvanceSvgfHistory. The profiler dummy-write blocks must stay where they sit.
  Effort LARGER.

- [O2] MEDIUM r_vulkan.cpp ~9683-9727 and ~10946-10992 — the two-pass bloom
  blur is written TWICE and the source says so ("Identical to the raster
  chain's -- this is DOOM-0331's shader, unmodified, driven the same way").
  The single delta is the final barrier's reader stage.
  Change: RecordBloomBlur(ext[3], finalReaderStage). Effort QUICK.
  DOOM-0360 is already queued to change this exact chain — fixing it now means
  that change lands once instead of twice.

- [O2] MEDIUM r_vulkan.cpp ~10056-10068 and ~10726-10754 — the world
  push-constant block is built twice and the two MUST agree ("mirror the raster
  weapon draw so the gun renders identically in Solid and Ultra"). Correctness
  is POSITIONAL: insert a field at index 17 in one and the weapon renders wrong
  in Ultra only, with no compile error and no test that would fire.
  Change: FillWorldPushConstants(pc) writing [0..23]. Effort QUICK.

- [O2] MEDIUM r_vulkan.cpp ~9259-9266 and ~10591-10594 — the render-scale clamp
  exists twice and the two copies HAVE ALREADY DRIFTED: RT rounds to even,
  floors at 2 and caps at the display extent; raster does none of those. Same
  dial, same stated intent, different results for the same input.
  OPEN QUESTION the lane could not settle: does TAAU require the even-sized
  extent only the RT copy produces? If so the raster copy is a different thing
  wearing the same name. Settle before shaping the helper. Effort QUICK.

- [O2] LOW r_vulkan.cpp ~10080-10088 and ~11043-11059 — the 2D overlay tail is
  duplicated between RT and raster recordings, same comment on both.
  Change: RecordOverlayAndMenuText(). Verify the overlay pipeline is
  render-pass-compatible with BOTH passes first — if not, the finding is void
  and the duplication is load-bearing. Effort QUICK.

- [O1] LOW r_vulkan.cpp ~10189 and ~10215 — the raster path builds the same
  sprite set TWICE per frame from identical arguments, into two destinations;
  the code says so. Change: memcpy the first result, guarded on capacity.
  Effort QUICK. VOID if RB_BuildSprites is not pure — read it first. Do NOT
  apply on the RT branch, where the two buffers differ by design.

- [O1] LOW r_vulkan.cpp ~9462-9604 — ~12 device-address driver calls per frame
  for buffers that change per level; one of them (spc.matEmis) re-fetches a
  value already sitting in pc.matEmisAddr IN THE SAME FUNCTION. The one-line
  fix for that duplicate carries no risk. Caching the rest is LARGER and needs
  a measurement first; g.vbuf/spriteVbuf/lightBuf must be cached PER SLOT.

- [O1] LOW r_vulkan.cpp ~9857 and ~9875 — the debug mode label dispatches a
  thread per display pixel (32,400 workgroups at 1080p) to paint a small text
  box, twice when the profiler is on. Debug frames only, never in a golden
  capture. Lowest-value item in the lane; reported because the mechanism is
  nameable, not because it is worth the budget.

- [O2] LOW r_vulkan.cpp ~9919-9937 and ~11103-11152 — the "lazily size a
  host-visible readback buffer then copy the image into it" block exists twice
  (shotverify and devshot). Only two copies, so the Rule of Three arguably says
  leave it; the sizing arithmetic is the part that would go wrong silently.
  Effort QUICK.

- [O3] LOW r_vulkan.cpp ~10182 and ~10234 — the two branches of BuildFrameInputs
  repeat three of four guard conditions and both compute `aspect` identically.
  Change: one outer guard, branch inside. Do NOT merge the bodies. Effort QUICK.

## Lane 4 — r_vulkan.cpp lighting, emitters, GI bake, level build

- [O1] HIGH r_vulkan.cpp ~8094 — AN ANIMATED TEXTURE CYCLING RE-ARMS THE FULL
  STATIC POINT-LIGHT CULL. RB_UPD_RETEX fires when any wall/flat live texture
  id changes, which includes an animated flat cycling — DOOM cycles those every
  8 tics (~4.4 Hz), and nukage/lava/blood appear on most maps including E1M1.
  That sets worldEmitDirty -> BuildStaticEmitterSet -> staticLightsDirty ->
  the full O(subsectors x staticEmitters) cull whose own header comment calls it
  "the ~8 ms/frame hotspot the DOOM-0170 CPU profiler pinpointed", plus a full
  O(numtris) mesh rescan. But a cycling flat changes a face's texnum and hence
  its Le; it does NOT move a vertex, so the ranking and centroids are
  bit-identical and only the three Le floats per slot can differ.
  Reads as a periodic hitch, not a lower average.
  Change: store each cached slot's source emitter index; compare emissive
  MEMBERSHIP on rebuild; identical -> a weaker staticLightLeDirty refreshing
  only Le; different -> the existing full rebuild. Effort LARGER. INFERRED.
  >>> This is the DOOM-0170 fix one level up: the cull was hoisted out of the
  >>> frame, and then a ~4.4 Hz trigger was left wired to it.

- [O1] HIGH r_vulkan.cpp ~7979-8041 — every subsector's 384-byte light slot is
  rebuilt and rewritten EVERY RASTER FRAME, including subsectors no dynamic
  light can reach. The seed distances are a pure function of the centroid and
  the cache, neither of which changes between frames; where no dynamic emitter
  wins, the record ends up byte-identical to what the slot already holds. The
  dynN<=0 path memcpys the whole cache over a buffer that already contains it.
  This is host-coherent write-combined memory, on Solid — the tier whose
  feature is performance.
  Change: store the worst kept distance per subsector; test dynamic emitters
  against it first and skip the seed, the recomputes and the write entirely.
  Track per in-flight slot whether it currently holds the pure static cache.
  Effort LARGER, needs before/after at one fixed render scale. INFERRED.

- [O1] MEDIUM r_vulkan.cpp ~7821-7834 — a bring-up diagnostic on the PER-FRAME
  path sums a per-level constant and does a synchronous stdout write whenever
  the lit-sprite count moves — i.e. whenever the player turns or a fireball
  spawns, not once at bring-up. The launch script tees stdout to disk.
  Change: accumulate the sum once in BuildStaticEmitterSet (which already walks
  the same weights); gate the print on the developer/profile gate. Effort QUICK.

- [O1] MEDIUM r_vulkan.cpp ~8450-8466 — the fog bake scores every clustered
  light against every air cell with no spatial pruning, although a light's
  contribution is exactly zero past its reach (capped at 8 cells), then fully
  sorts a candidate list to consume four entries. No longer level-load-only:
  it re-runs per settled door, and DOOM-0011 4.4 gates it at <=6 ms.
  Change: scatter each light into the cells inside its own reach box; replace
  std::sort with std::partial_sort. Scatter LARGER, partial_sort QUICK.
  CAVEAT: partial_sort orders equal scores differently — add an explicit
  tie-break on light index or determinism is lost.

- [O1] MEDIUM r_vulkan.cpp ~7730-7741 — the static half of the per-emitter
  sector buffer is rewritten with a constant sentinel, into write-combined
  memory, every traced frame. Change: write the prefix once where staticN
  becomes known. Effort QUICK. Tie the refill to the same flag the point-light
  cache uses, so there is one trigger.

- [O1] LOW r_vulkan.cpp ~7776 (and nee_sampling.h) — five heap allocations per
  frame in the emitter refill, in a file that already has the persistent
  scratch idiom two functions away. The per-frame budget is fixed and known.
  Effort QUICK for r_vulkan.cpp; LARGER for the header (shared contract + test).

- [O2] MEDIUM r_vulkan.cpp (five sites) — the emitter record's geometry maths
  is written FOUR times (triangle-area cross product x3, centroid x4) and the
  14-float stride is a literal in FIVE places, though nee_sampling.h already
  defines NEE_EMIT_STRIDE and this same file uses the shared emis::luminance
  helper for the other half of the very same calculation. Le is addressed as
  raw offsets 9/10/11 in three functions; one site documents the layout by
  citing line numbers in another file. Effort QUICK.

- [O2] MEDIUM r_vulkan.cpp ~7883-7896 vs ~8018-8037 — the nearest-N insertion
  sort THE WHOLE CACHE DESIGN RESTS ON exists twice. The merge's correctness
  argument holds only while both copies use the same metric and tie behaviour;
  a divergence would not crash and no test would catch it. Effort QUICK.

- [O3] LOW r_vulkan.cpp ~7945-7954 — the haveCache == false branch cannot be
  reached; three defensive branches imply a failure mode the function cannot be
  in. Change: drop haveCache, assert the sizes once. Effort QUICK.

- [O4] LOW r_vulkan.cpp ~8717 — a push-constant assignment whose own comment
  says nothing reads it ("unused by mode 5", in a function that dispatches
  mode 5 exclusively). Effort QUICK.

## Lane 9 — playsim world core (p_map, p_maputl, p_mobj, p_sight, p_setup, p_tick)

NOTE: no HIGH findings, and the lane says so deliberately — it found no
per-tic work that could be hoisted WITHOUT changing simulation results, and
demo compatibility makes such a change inadmissible. Claiming a HIGH would
have meant inventing a frame-time win it could not demonstrate.

- [O4] MEDIUM six sites — dead globals and one empty function, each verified
  unreferenced tree-wide: p_mobj.c `int test;`; p_tick.c `P_AllocateThinker`
  (empty body, no prototype, no caller, and a comment describing something it
  does not do); p_maputl.c `int ptflags;`; p_sight.c `sightcounts[2]`
  (incremented twice, read nowhere); p_map.c `secondslidefrac`/`secondslideline`
  (written together, read nowhere); p_local.h `MAPBMASK`.
  secondslide* is the costly one to a reader — it looks like the second-best-
  line half of a slide algorithm, so anyone touching P_SlideMove must first
  prove it is vestigial. P_AllocateThinker actively misleads: it appears to be
  the thinker allocation path and is a no-op. Effort QUICK, zero sim risk.

- [O2] MEDIUM p_sight.c ~108-128 — P_InterceptVector2 is a byte-for-byte
  duplicate of p_maputl.c's P_InterceptVector, down to the same commented-out
  I_Error. One caller. Two copies of the engine's demo-critical fixed-point
  intercept formula: a "fix" applied to one desynchronises sight checks from
  path traversal, quietly. Effort QUICK, provably identical by inspection.

- [O2] MEDIUM p_map.c ~130-163 and ~392-432 — P_TeleportMove and
  P_CheckPosition carry the same ~30-line prologue and the same block sweep,
  character for character. Any future change to what "set up tm* state" means
  must be made twice, and a miss in P_TeleportMove shows only on a teleport.
  Effort QUICK — pure code motion in one file; validcount++ must stay exactly
  where it is relative to the sweep.

- [O2] MEDIUM p_mobj.c (four sites) + p_inter.c + p_enemy.c — the state-tic
  jitter idiom (`tics -= P_Random()&3; if (tics < 1) tics = 1;`) is written
  SIX times. Rule of Three exceeded twice over. Each copy consumes exactly one
  P_Random, so a future edit that changes the mask on five sites and misses the
  sixth is a demo desync, not a visual glitch. Effort QUICK.
  The helper must be called WHERE the inline code stood, never hoisted.

- [O1][O2] LOW p_mobj.c ~612-620 and ~769-777 — the doomednum-to-mobjtype
  lookup is a linear scan over all mobjinfo entries, written twice; the only
  two such lookups in the tree. On a large PWAD with thousands of things that
  is a few hundred thousand comparisons at load. Change: build the map once in
  P_Init. Effort QUICK. MUST keep the FIRST match in ascending order — 19
  mobjinfo entries share doomednum -1 (DOOM-0397 records this).

- [O1] LOW p_setup.c ~577-578 — P_LoadBlockMap runs a full read-modify-write
  pass over the entire BLOCKMAP lump calling SHORT(), which m_swap.h defines as
  the IDENTITY macro on this little-endian build. Vanilla maps make it trivial;
  a large modern PWAD's BLOCKMAP is hundreds of KB to a few MB.
  Change: wrap in #ifdef __BIG_ENDIAN__, matching m_swap.h's own conditional.
  Effort QUICK. Not verified whether the compiler already elides it.

- [O1] LOW p_maputl.c ~714-749 — P_TraverseIntercepts is an O(n^2) selection
  sort per path traversal (128 cap => 16,384 comparisons worst case), called
  several times per player tic while moving and shooting. Bounded by the cap,
  so it does NOT get worse on large PWADs.
  LANE'S OWN RECOMMENDATION: leave it alone unless a profile names it. A
  replacement sort must be STABLE — equal-frac intercepts are visited in
  blockmap-walk order today, and reordering ties changes which line a hitscan
  hits first, which is a demo break. Effort LARGER.

- [O2] LOW p_setup.c (five sites) — allocate-then-zero written out five times,
  with the memset recomputing `count * sizeof(elem)` by hand — the exact
  product P_LevelAlloc exists (DOOM-0399) to keep from overflowing.
  Change: add P_LevelCalloc doing both from one size expression. Effort QUICK.

- [O2] LOW p_sight.c ~212-221 vs p_maputl.c ~315-329 — the two-sector opening
  calculation is written twice and HAS ALREADY DRIFTED: the sight copy carries
  a "because of ceiling height differences" comment above the FLOOR branch,
  copied from the line above. The sight copy also shadows the globals the other
  sets. Effort QUICK. p_sight.c must keep using LOCALS, never the globals.

- [O2] LOW p_map.c ~844-946 — PTR_AimTraverse and PTR_ShootTraverse are
  near-copies differing only in what they do with the slope (~25 duplicated
  lines of the fixed-point arithmetic every hitscan weapon depends on).
  LANE'S OWN CAVEAT: least confident finding; the shared part is smaller than
  it looks once the differing control flow is stripped. Worth a look, not a
  mandate. Effort LARGER.

- [O4] LOW p_maputl.c x2, p_setup.c x1 — three compiled-out blocks no build
  reaches, including a whole float reimplementation of P_InterceptVector. The
  p_setup.c one has a real readability cost: a live Z_FreeTags call is indented
  under a dangling `else` and reads as conditional when it is not.
  TENSION the lane flags for the maintainer: all three are id's own 1997 text,
  and this fork deliberately preserves that. If compiled-out vanilla counts as
  historical reference (the posture taken toward sndserv/sersrc/ipx), KEEP them
  and close the finding — but then add a one-line comment saying the Z_FreeTags
  call is unconditional.

## Orchestrator note — lane 5's open question, SETTLED

Lane 5 could not tell whether TAAU requires the even-sized render extent that
only the RT copy of the render-scale clamp produces, and flagged it as blocking
the shape of any shared helper.

Answered by reading shaders/taau.comp: it takes render W/H as arbitrary values
in a push constant, and both sampleColor and sampleHist do bilinear fetches
clamped to the dimensions passed in. There is no 2x2 quad reconstruction, no
even-size assumption, and no masking anywhere in the shader.

So the even rounding is NOT a TAAU requirement. It is defensive or vestigial.
Consequence for the refactor: the helper does not need to force even, and the
raster copy must keep its current arithmetic regardless to stay bit-identical
under -shotcompare. Whoever takes that item should confirm nothing ELSE
downstream of the RT extent depends on evenness before removing the rounding —
this settles the TAAU half only.

## Lane 7 — GLSL shaders

- [O1] HIGH svgf_atrous.comp ~80 — `pow(x, 64.0)` IS EVALUATED 96 TIMES PER
  PIXEL PER FRAME. 24 neighbour taps x 4 host iterations, in a pass whose own
  host comment measures it at "~36 ms at native / ~8.5 ms at 50%".
  THE PROJECT HAS ALREADY MEASURED THAT GLSLC DOES NOT FOLD A CONSTANT
  EXPONENT: pt_common.glsl's DOOM-0295 note records "33 Pow ops survive in
  pathtrace.comp" for a 1.5 exponent, which is why fogPhaseHG was rewritten as
  an inversesqrt cubed. Same trap, unfixed here.
  Change: repeated squaring — six multiplies instead of log2+mul+exp2 (two
  quarter-rate transcendentals). sigN stops being a tunable float and becomes
  the exponent in the code; say so, as DOOM-0295's precedent did.
  Effort QUICK. Largest single frame-time item in the sweep.
  Verify: shaderstats Pow-op count A/B, then -rtverify + -shotcompare.

- [O1] MEDIUM pathtrace.comp (8 sites) — the de-tile world key
  `detileWorldUV(hitP,n)/kDetileWorldCell` is recomputed FOUR times per hit, in
  EACH of mode 4 and mode 6; and `pc.triSs.s[prim]` is loaded twice per hit in
  each. Mode 6 is the shipped Ultra play path, so this is every grid-sample
  pixel of every frame. ~18 wasted ALU ops per HD hit plus a redundant
  dependent buffer load. The line above already says "HOISTED".
  Change: hoist both once after hitP. Effort QUICK, no output change.

- [O2] HIGH pathtrace.comp ~1496-1632 vs ~1682-1842 — the mode-4 display path
  and the mode-6 denoise-feed path are ~70 LINES OF THE SAME SHADING LOGIC
  WRITTEN TWICE, several comments duplicated verbatim. The two are REQUIRED to
  agree: demodulation only works if mode 6's illum is mode 4's L with albedo
  factored out. An edit to one silently desynchronises the raw `~` view from
  the shipped denoised view, and nothing in the build catches it. DOOM-0130
  already consolidated the muzzle/flashlight halves of this same pair for this
  same reason; the rest was left behind.
  Change: one shadeHitCommon() parameterised by albedo factor and addEmission.
  Effort LARGER. REAL HAZARD: the mode spec-constant currently lets the driver
  dead-strip each branch — a shared function must not defeat that. Measure VGPR
  count and occupancy, not just correctness.

- [O4] MEDIUM pathtrace.comp ~309 — the `misc` push lane's x component IS DEAD
  (DOOM-0129 moved the mode to a specialization constant; the shader says so in
  a comment) yet the host still writes it at two sites, AND
  docs/standards/renderer.md's lane table still lists it as live and its
  closing paragraph tells the next feature it must grow the push range to 256 B.
  So a stale standard is about to cost a real change. Effort QUICK.
  >>> Fix the shader, the two host writes AND the standard in one change.

- [O1] MEDIUM pathtrace.comp ~718, ~744 — hash3() is evaluated TWICE per
  de-tile cell (once inside detileCellUV, once again for the mirror test), 2-4
  taps per pixel, in both the normal and POM paths. Four pcgHash rounds each.
  The megakernel is issue-bound, so integer ALU competes directly.
  Change: pass the hash instead of the cell. Effort QUICK, bit-identical.

- [O2] MEDIUM bloom_extract_raster.comp vs bloom_extract_rt.comp — the
  soft-knee bright-pass weight is written out twice, plus a duplicated
  non-finite guard and tap-loop preamble. The RT file's header says the
  arithmetic is "deliberately not re-derived here in any other form" — which is
  the intent, and the copy is the mechanism that defeats it. The project has
  been bitten by this class twice already (scene_recombine.glsl and
  tonemap_encode.glsl both exist for exactly this reason).
  Change: formulas/bloom_knee.glsl. Effort QUICK.
  TRAP: glslc emits no auto-dep for a GLSL include — the Makefile dep line must
  be added or an edit to the new header will not rebuild its dependants.

- [O2] MEDIUM mesh.frag ~152-165 — giIrradiance is a VERBATIM copy of
  pt_common.glsl's, and mesh.frag's own comment says so. If either is
  re-derived, Solid and Ultra read the same baked probes and shade them
  differently — a tier mismatch no test compares. Change: formulas/gi_probe.glsl.
  Effort QUICK. It must come OUT into its own file: pt_common.glsl has a header
  contract mesh.frag cannot satisfy.

- [O2] MEDIUM pathtrace.comp ~412-417 — toneEncode() is a THIRD copy of the
  shared tone operator; formulas/tonemap_encode.glsl exists and lists two
  consumers, and pathtrace.comp was never migrated. The last change to the
  operator (DOOM-0345 R1) touched two of the three copies. Effort QUICK.

- [O1] MEDIUM pt_common.glsl ~599-608 — every NEE sample BINARY-SEARCHES the
  emitter CDF, six times per shaded pixel, each step a DEPENDENT load. On a map
  with low-thousands of static emitters that is ~66 serialised loads before a
  single shadow ray is cast. directNEEVerify carries a second copy of the search.
  Change: Vose alias table — one load + one compare, O(1), no dependent chain.
  Effort LARGER (crosses nee_sampling.h, the buffer build, the push block, two
  shaders). LANE'S OWN ADVICE: measure the emitter count on a real map FIRST; if
  it is small, drop this rather than building it.

- [O1] LOW mesh.frag ~329-406 — cos/sin of pc.yaw computed twice and
  normalize(vNormal) twice, on the Solid tier whose feature is frame time. May
  already be folded; one site is inside a branch, which is the case CSE most
  often misses. Do NOT move cos/sin into the push block — it is shared
  byte-for-byte with mesh.vert and sits at 124 B against a 128 B floor.

- [O1] LOW ssao.frag ~90-92 — normalize() re-derived at runtime for a const
  kernel table whose own comment says "Normalized in use", plus a ramp fixed at
  author time. 16 rsqrt per half-res pixel if not folded. bloom_blur.comp
  already uses the pre-computed-constant convention. Effort QUICK.
  Check with shaderstats whether the driver already folds it; if so, drop.

- [O1] LOW label.comp ~47-64 — full-frame dispatch (~2M invocations at 1080p)
  to draw a small debug label box. Confirms lane 5's independent finding of the
  same thing from the host side. Effort QUICK-per-file, crosses host+shader.

- [O2] LOW — the bindless material-id mapping is written out in SEVEN places
  and the barycentric cut-out test in FOUR. Effort QUICK for the
  pathtrace-internal half; the raster pair needs the shared-header treatment.

- [O2] LOW svgf_temporal.comp vs svgf_composite.comp — the previous-frame
  reprojection is written twice; lum() is defined twice byte-identically.
  Change: formulas/reproject.glsl. Effort QUICK.
  IMPORTANT: a sign flip here shows as violent ghosting IN MOTION, and
  -shotcompare's golden frames are STATIC so they would NOT catch it.
  Verify by playing, not by capture.

## Lane 6 — C-side renderer seam (r_mesh, r_backend, rb_image, rb_text)

- [O1] HIGH r_backend.c — R_MarkAutomapLines runs a FULL SOFTWARE BSP TRAVERSAL
  every 3D frame, though its result can only change on a game tic. It is handed
  the RAW player, not the interpolated camera, so between tics it provably
  recomputes an identical answer. Change: gate on gametic. Effort QUICK.
  ML_MAPPED is sticky so a skipped frame loses nothing; the risk is a hidden
  dependency on R_SetupFrame's other side effects.
- [O1] HIGH r_mesh.c — RB_UpdateMeshHeights rescans EVERY mesh vertex every
  frame, though every input it reads (sector heights, texture translation)
  changes only on a tic. Effort LARGER: the destination is double-buffered per
  in-flight slot, so a single global "last tic" is NOT enough — key it per
  destination. Must not break animated-texture cycling (DOOM-0066).
- [O1] MEDIUM r_mesh.c — the per-frame loop READS BACK the mapped GPU vertex
  buffer twice per vertex, to compute two booleans. This is the exact mistake
  the project already paid for once on a different mapped buffer. Change: keep a
  plain-RAM shadow. Effort LARGER. Do it WITH the finding above or not at all.
- [O3] MEDIUM r_mesh.c (four sites) — a workaround repeated four times whose
  stated cause DOES NOT EXIST. Four comments claim subsectors[].sector "is not
  populated at load in this DOOM build"; p_setup.c's P_GroupLines fills it for
  every subsector, and THIS SAME FILE already relies on that elsewhere. One
  false belief restated four times, in the file that defines world-to-mesh.
  Effort QUICK. If the belief is right, the other site is the bug instead.
- [O4] MEDIUM r_backend.h — the SetResolution slot in the back-end seam is NEVER
  DISPATCHED; six declarations serve nothing, in the file the renderer standard
  names as THE seam. Note DOOM-0026's spec describes the slot, so removing it is
  a spec amendment. Effort QUICK (edit) / LARGER (with the amendment).
- [O2] MEDIUM r_mesh.c — the "is this line open?" test is written twice and the
  two ALREADY READ DIFFERENT FIELDS (seg's backsector vs linedef's), while the
  detector's whole purpose is to track what the flood would do. Effort QUICK.
- [O1] MEDIUM r_mesh.c — RB_RefreshSunClearance mallocs, copies and memcmps a
  full snapshot of the field on every call, purely to return a flag; up to
  512 KB per call, on frames that are already the expensive ones. Change: have
  the march report whether anything changed. Effort QUICK.
- [O2] LOW r_backend.c — the valid rb_rtdebug set is spelled twice in two
  different forms, with the coupling documented in prose. Effort QUICK.
- [O3] LOW r_backend.c — a hand-rolled byte loop where the same file uses memset
  eight lines later. Effort QUICK.
- [O2] LOW r_mesh.c — every sprite patch header is read twice at startup, and
  one path re-reads a width the engine already tabulates. Effort QUICK.
- [O2] LOW r_mesh.c — the cap-fan and triangle push each exist in a world copy
  and a near-identical sky copy, ALREADY drifting (one tags vsector/vplane for
  re-height, the other does not). Second copy, so Rule of Three says hold.
- [O2] LOW r_mesh.c — the three billboard builders repeat the thing walk (two
  full traversals of every sector's thing list per frame) and the sprite-metrics
  block. Effort LARGER. The two walks skip DIFFERENT things — fusing them
  carelessly drops billboards.
- [O1] LOW rb_image.c — rb_devshot_path rescans every previously written shot on
  every call: O(N^2) over a capture session. Bites the headless harness.

## Lane 8 — software renderer (Classic tier)

CONSTRAINT the lane applied throughout: an O1 here is admissible only if
provably output-identical, with the proof named. It dropped two candidates
for failing that bar and says so.

- [O1] HIGH r_data.c — R_CheckTextureNumForName is a LINEAR strncasecmp SCAN,
  and level load calls it three times per sidedef. O(numsides x numtextures)
  case-insensitive compares per P_SetupLevel: on a large PWAD, millions.
  Change: build a name->index table once in R_InitTextures. Effort LARGER.
  MUST preserve first-match-wins (the current loop returns the LOWEST index
  among duplicate names).
- [O4] MEDIUM ~20 symbols across r_draw/r_main/r_plane/r_things + 3 headers —
  port vestiges (viewimage, translations[3][256] superseded by
  translationtables), profiling leftovers (dccount, dscount, framecount,
  linecount, loopcount), unused storage (spanstop = 1600 bytes of .bss,
  floorfunc/ceilingfunc, maskdraw_t, newvissprite) and EIGHT HEADER
  DECLARATIONS WITH NO DEFINITION — r_things.h advertises three sprite entry
  points that were never ported, r_bsp.h three light tables.
  WORSE THAN DEAD: r_plane.h declares `extern planefunction_t ceilingfunc_t;`
  and the definition is named `ceilingfunc` — that extern names an object that
  does not exist, and only nothing referencing it keeps the link working.
  Effort QUICK, near-zero risk (a real reference fails the link).
- [O1] MEDIUM r_draw.c (5 sites) — SCREENWIDTH is a RUNTIME GLOBAL read inside
  every per-pixel loop, and the byte store may alias it, so the compiler must
  reload it every pixel. Written once at startup (I_InitWidescreen, before
  R_Init) so hoisting to a local is output-identical by construction.
  Effort QUICK. Perf claim INFERRED from the aliasing rule, not a disassembly.
- [O1] MEDIUM r_plane.c (2 sites) — every new visplane clears MAXWIDTH columns
  whatever the view width is. DOOM-0055 widened these from byte to unsigned int
  (x4) and DOOM-0147 raised the array to the MAXWIDTH cap (x2 more at 4:3).
  ~5 KB per visplane created, two sites, every frame. Only [minx-1..maxx+1] is
  ever read. Change: clear viewwidth columns. Effort QUICK.
- [O1] MEDIUM r_draw.c — the fuzz column multiplies by a global per pixel to
  rebuild a table DOOM-0147 flattened. Paid per pixel of every spectre and every
  invisible sprite — worst case is a room full of spectres, which is exactly
  when the renderer is already busiest. Change: precompute in R_InitBuffer,
  which already rebuilds ylookup/columnofs on geometry change. Effort QUICK.
- [O2] MEDIUM r_segs.c (2 sites) — the wall light-level selection is written
  twice VERBATIM in one file, both carrying the same "OPTIMIZE: get rid of
  LIGHTSEGSHIFT" comment. Two more near-copies of the clamp in r_things.c —
  four copies of one clamp, in a project whose whole Phase 2 is lighting work.
  Effort QUICK for the r_segs pair; leave r_things in the same change.
- [O2] MEDIUM r_data.c (3 sites) — three near-copies of the per-patch column
  walk, ALREADY DRIFTED: the atlas path clamps to tilew, the composite path to
  texture->width. So Classic and Solid/Ultra now disagree about a patch whose
  origin puts it past the texture width. Lane could not tell if that is
  deliberate. Effort LARGER. Lane's least-confident finding, flagged as such.
- [O3] MEDIUM r_things.c — R_DrawVisSprite takes x1 and x2 and never reads
  them; both callers pass exactly the struct members it uses instead. The
  signature advertises a sub-range draw the function does not honour.
- [O3] LOW r_main.c — a 4096-iteration fencepost loop computes a value it never
  reads (two dead statements that look like setup the live fixups depend on).
- [O3] LOW r_bsp.c/r_segs.c — two cross-module declarations hand-written inside
  .c files (one extern repeated in two function bodies) instead of in the
  headers that exist for them. A hand extern is not checked against its
  definition.
- [O1] LOW r_data.c — R_PrecacheLevel caches a rotation-free sprite's lump
  EIGHT times and adds its size eight times, so spritememory is overstated up
  to 8x. Most non-monster sprites are rotation-free: the common case.
- [O1] LOW r_things.c — R_DrawSprite rescans every drawseg per sprite, including
  the ones whose sprite-INDEPENDENT test can never clip anything. Bounded by the
  caps so it does NOT worsen on large PWADs. Effort LARGER; order is the whole
  risk (the scan runs downward and first-qualifying wins).
- [O4] LOW r_main.c/r_plane.c/r_sky.c — three startup functions with EMPTY
  BODIES, each called from R_Init and each followed by a printf naming it, so
  the startup log claims three init steps that are no-ops. R_InitPlanes is where
  a reader would naturally put visplane setup and is a stub.
  DECISION, not a cleanup: the #if 0 bodies are id's own markers recording that
  tables.c superseded them, and this fork preserves the 1997 source. Deleting
  the CALLS and printfs while keeping the functions is the middle option.
  CHECK FIRST: harnesses grep game stdout.
- [O4] LOW r_draw.c/r_main.c/r_segs.c — ~200 lines of #if 0 alternatives, one of
  which HAS NEVER COMPILED (`usingned spot;`) — which is the proof it is
  unreachable rather than merely disabled. Plus two commented WATCOM/VGA blocks
  (outpw, GC_INDEX) that are DOS hardware the SDL2 port replaced outright.
  IMPORTANT: low-detail mode itself is NOT dead — detailLevel is read from
  ~/.doomrc and passed to R_SetViewSize, so R_DrawColumnLow/R_DrawSpanLow still
  run. Do not treat the Low drawers as dead because the menu row is disabled.
- DROPPED by the lane, deliberately: R_SortVisSprites' O(n^2) selection sort
  (tie-break reproduction costs more than the win) and the 1.3 MB static
  visplanes array (any fix changes output).
- LANE'S OWN GAP REPORT: performance.md prescribes the per-pass GPU profiler,
  which does not reach a CPU software renderer. Classic has NO measurement
  procedure in the standard. Worth filing separately.

## Lane 10 — playsim actors and specials

- [O2] HIGH p_doors.c/p_floor.c(x2)/p_ceilng.c + siblings — the
  spawn-a-sector-thinker sequence is written out seven times; its absence has
  DEMONSTRABLY already caused divergence between files that should agree.
- [O2] MEDIUM p_lights.c — EV_TurnTagLightsOff hand-rolls twelve lines of
  P_FindMinSurroundingLight, which the SAME FILE already calls four times.
  Provably identical, one line. The clearest instance of the reuse rule here.
- [O2] MEDIUM p_spec.c — five neighbour-scan functions differing only in seed.
- [O2] MEDIUM p_ceilng.c/p_plats.c — eight functions over two parallel arrays.
- [O2] MEDIUM p_enemy.c — A_KeenDie and A_BossDeath carry the same walk.
- [O2] MEDIUM p_doors.c — six near-identical keycard blocks.
- [O2] MEDIUM p_inter.c — P_TouchSpecialThing is three tables written as 290
  lines of switch.
- [O2] LOW — adjacent switch cases differing by one value (p_plats/p_doors);
  three copy-paste sets in p_enemy; P_ChangeSwitchTexture's top/middle/bottom
  arms are one body; four weapon-fire routines share a five-line preamble;
  T_MovePlane repeats one move-and-revert block eight times.
- [O1] MEDIUM (inferred) p_enemy.c — A_PainShootSkull walks the ENTIRE thinker
  list to count lost souls.
- [O1] MEDIUM (inferred) p_telept.c — EV_Teleport scans every sector then walks
  its thing list.
- [O1] MEDIUM (inferred) p_spec.c — every "find sectors by tag" is a LINEAR SCAN
  over all sectors, called repeatedly. The O1 shape most likely to bite on large
  modern maps.
- [O1] LOW (inferred) p_spec.c — P_UpdateSpecials recomputes every animated
  texture each tic.
- [O3] MEDIUM p_user.c — P_CalcHeight writes player->viewz THREE times in the
  same call; the two dead stores invite a reader to "fix" an airborne
  view-height clamp that is deliberately absent.
- [O3] LOW p_doors.c — P_SpawnDoorRaiseIn5Mins takes a secnum it never uses.
- [O3] LOW p_plats.c — T_PlatRaise's pastdest switch: four cases, one action.
- [O4] MEDIUM p_pspr.c — P_CalcSwing, swingx and swingy reachable from nothing.
- [O4] LOW p_spec.h — two macros nothing uses; the sliding-door code, disabled;
  four externs in the file that defines them.

## Lane 11 — game loop, state, save, net

- [O1] HIGH Makefile — THE ENGINE SHIPS WITH NO OPTIMISER FLAG. See the
  orchestrator's measured verdict at the end of this file.
- [O3] HIGH d_main.c — D_DoomMain is 455 lines doing nine unrelated jobs, and
  TWO LOAD-BEARING ORDERING CONSTRAINTS live only as comments inside it
  (defaults must load before I_InitWidescreen; -nosound must be read before
  I_Init opens the device). Both have been got wrong before (DOOM-0147 Part C,
  DOOM-0327). Change: six functions preserving call order exactly, so the
  constraint is visible in the call sequence. Effort LARGER.
- [O3] MEDIUM g_game.c — G_DoCompleted tests gamemap 8 and 9 twice; the second
  map-8 block is UNREACHABLE (the switch returns) and the second map-9 block
  re-sets a value it already holds. id's own `//#if 0 Hmmm - why?` was turned
  into a comment, so both copies compile. Effort QUICK.
- [O2] MEDIUM m_misc.c — M_SaveDefaults hand-copies the string/int test that
  M_DefaultIsString exists to own, and THAT FUNCTION'S OWN COMMENT claims it has
  no third caller. So the comment is false, and what would break is the config
  WRITER producing a file the parser then rejects. Effort QUICK, one line.
- [O2] MEDIUM d_main.c — a SECOND IWAD classifier, case-sensitive and
  substring-matching, beside the unit-tested exact-match one in iwad_detect.h
  that exists precisely so the two cannot drift. They HAVE drifted: `DOOM2.WAD`
  is commercial to one and registered to the other. A third copy of the name
  list is hand-built as seven snprintfs; a fourth walks it as seven access()
  tests. Four statements of "which files are IWADs", one tested. Effort LARGER.
  Auto-detect PREFERENCE ORDER differs between the two and is observable.
- [O2] MEDIUM g_game.c — the commercial sky block is written twice, the copies
  HAVE DRIFTED (one carries DOOM-0139's gamemission fix, the other does not),
  and G_InitNew's copy is overwritten by G_DoLoadLevel's on the same call.
  Effort QUICK. Admissible: skytexture is renderer-only, never in the demo
  stream, never touches rndindex.
- [O2] MEDIUM p_saveg.c — P_ArchiveSpecials is eight copies of one eight-line
  block; two emit byte-identical records. The sector swizzle is written eight
  times each way. DOOM-0374's bounds conversion was already sixteen edits, and
  an arm missed in such a pass writes an unbounded record. Effort LARGER.
  THE ON-DISK LAYOUT IS THE RISK: padding is position-dependent, and DOOM-0426's
  signature will NOT catch a reorder because the struct sizes are unchanged.
  Proof required: cmp two .dsg files byte for byte.
- [O4] MEDIUM nine unreferenced globals/functions across d_main/d_net/g_game +
  m_misc. Two are traps: `wadfile[1024]` sits one character from the live
  `wadfiles[]`, and `-noblit` IS PARSED AND NEVER READ — a documented timedemo
  switch that silently does nothing, where its sibling nodrawers works.
- [O4] MEDIUM d_main.c/g_game.c — four 1997 DOS-era switches whose targets do
  not exist in any build: -cdrom (calls mkdir("c:\\doomdata", 0), which on Linux
  creates a directory literally named that, mode 0), -shdev/-regdev/-comdev
  (id's internal dev tree), -wart (rewrites its own argv). Effort LARGER — the
  game-select guard at d_main.c names all three dev flags.
- [O4] LOW m_misc.c — two 1997 serial-mouse settings (mousedev "/dev/ttyS0",
  mousetype "microsoft") are LIVE in the defaults table and written into every
  player's ~/.doomrc on every quit; nothing reads either. The mouse is SDL2's.
- [O4] LOW four preprocessor arms no build can select, including a d_net.c
  `#else` holding the UNBOUNDED form of a write the live arm guards — flipping
  `#if 1` to `#if 0` would reintroduce an out-of-bounds write from a packet.
- [O1] LOW d_main.c — D_ProcessEvents runs a full-WAD linear lump scan EVERY TIC
  to answer a question settled once at startup. Effort QUICK.
- INCIDENTAL (lane went looking for none, found one): d_main.c D_AddFile scans
  wadfiles[] for the first NULL with NO BOUND against MAXWADFILES. `-file` with
  twenty or more WADs writes past the array; at exactly twenty it leaves no NULL
  terminator for W_InitMultipleFiles to stop on. Command-line only.

## Lane 12 — UI, HUD, menu, intermission, finale, automap, 2D primitives

- [O1] HIGH m_menu.c — the Load/Save menu does ~156 FULL LUMP-DIRECTORY SCANS
  PER FRAME: W_CacheLumpName is called inside the border loop, and
  W_CheckNumForName is a backwards linear scan of ~2300 entries. Same shape in
  M_DrawThermo. Change: hoist the lookup out of the loop — nothing allocates
  between the draws, so the PU_CACHE block cannot be purged mid-loop.
  Effort QUICK. Cheapest real win in the lane.
- [O1] MEDIUM st_stuff.c + d_main.c — the status bar's whole DIFF-DRAW machinery
  is BYPASSED in Solid and Ultra, because d_main passes `rendermode !=
  RB_CLASSIC` into refresh, which is constant-true there. So the full bar is
  repainted every frame: a 320x32 V_CopyRect, the widescreen side-fill, 11
  number erase+draw pairs and 10 icons. oldnum/oldinum exist to prevent exactly
  this and never get the chance. Effort LARGER, measure first.
  Lane could NOT close the risk: it did not check the Vulkan compositor.
- [O1] MEDIUM v_video.c — V_CopyRect expands every pixel with a scalar f x f
  store loop with RUNTIME trip counts, so it cannot vectorise; every live caller
  is the status bar, i.e. the every-frame path above. Change: build one
  destination row and memcpy it to the rest. Effort QUICK, output-identical.
- [O1] MEDIUM am_map.c — AM_drawWalls dereferences BOTH vertices of every
  linedef before testing visibility, and on a fresh level almost nothing is
  ML_MAPPED. Four pointer-chased loads per linedef per frame, nearly all
  discarded. Change: test first, fill after. Effort QUICK.
- [O1] LOW f_finale.c — the finale text screen re-tiles the WHOLE physical
  screen from the flat every frame (~683 KB of memcpy at 1708x400, 35x/sec)
  although the tiling never changes. WI_slamBackground already shows the fix.
- [O2] HIGH v_video.c — THREE near-copy patch blitters (~270 lines), and the
  drift is ALREADY ON THE RECORD: V_DrawPatchScaled shipped WITHOUT the bounds
  guard both siblings carried, and its own comment says so — a WAD-supplied
  offset could index screens[] out of range. Two further copies of the post walk
  live in m_menu.c and f_finale.c. Effort LARGER. This is the path that turns
  WAD bytes into frame-buffer writes.
- [O2] HIGH (cross-file) — the hu_font measure-and-draw idiom is written SIX
  times in this lane (a ninth copy, M_DrawText in m_misc.c, is called by
  nothing). They have ALREADY DRIFTED into THREE DIFFERENT CLIPPING POLICIES:
  M_WriteText clips x only, M_WriteTextScaled clips x and y, HU_DrawFPS and
  HU_drawSecret clip neither, HUlib uses `c <= '_'` instead of HU_FONTSIZE.
  Effort LARGER — unifying is a behaviour decision, not a pure refactor.
- [O2] MEDIUM m_menu.c — the scroll-window algorithm exists twice and THE CODE
  SAYS SO: DOOM-0403's comment records that DOOM-0206's repair to the crisp
  renderer "never reached this Classic-tier twin", and a HUD-safe overrun
  shipped because one copy was fixed. Effort QUICK.
- [O2] MEDIUM m_menu.c — the Classic renderer/effects menus hand-write sixteen
  label+value pairs over the same variables the crisp value provider already
  switches on, with the same range clamps repeated verbatim. The file already
  proves the fix works (M_DrawDevMenu calls M_DevCrispValue). Effort LARGER.
- [O2] LOW m_menu.c — the render-scale index scan written three times: exactly
  the Rule-of-Three threshold.
- [O3] HIGH m_menu.c — 4009 lines: ONE ~1,200-line framework plus FIVE payloads
  that do not reference each other (save/load, video/renderer, developer — five
  separate #ifdef islands, game select, new game). The mixing has already caused
  a workaround: the file hand-copies two prototypes because <unistd.h>, pulled
  in ONLY for the save code, collides with p_local.h's `close` enumerator.
  The framework is not clean either — M_Drawer hard-codes two menu identities.
  Change: m_menu_local.h + five translation units; the dev one compiles only
  under DEV=1 and can then include p_local.h properly. Effort LARGER.
  DO THE REGISTRY WORK FIRST — it deletes ~95 lines and makes the split mostly
  mechanical.
- [O3] MEDIUM st_lib.c — STlib_drawNum's `refresh` parameter is DEAD and its
  header comment describes a diff-draw mechanism that is not there: it assigns
  oldnum and never reads it. Threaded through two call layers and eleven call
  sites. Its two siblings DO use theirs.
- [O3] LOW v_video.c — V_DrawPatchDirect is a pass-through wrapper around a
  50-line commented-out DOS planar-VGA corpse; 25 call sites use two spellings
  for one behaviour.
- [O4] MEDIUM f_wipe.c — the colour-transform wipe is UNREACHABLE (only
  wipe_Melt is ever passed), ~55 lines plus a dispatch table indexed *3 to
  select between two entries, one of which cannot run.
- [O4] MEDIUM wi_stuff.c — `if (commercial) return;` tests the ENUM CONSTANT
  (value 2), not `gamemode == commercial`, so WI_drawAnimatedBack returns on
  EVERY gamemode and the DOOM 1 intermission animations have never drawn. Its
  two siblings written the same day do it correctly. Everything feeding it —
  three anim tables, the per-tic state machine, the per-level WIA* lump loads —
  is dead weight. ONE-WORD DECISION, NOT A DELETION: fixing it RESTORES a
  shipped feature and CHANGES what the intermission looks like. Needs a
  behaviour call and a golden-shot refresh.
- [O4] MEDIUM v_video.c — V_GetBlock is never called, and received a DOOM-0402
  bounds fix for a guard nothing can exercise.
- [O4] MEDIUM am_map.c — AM_updateLightLev is dead (its only call is commented
  out), so lightlev is a constant zero added at nine draw sites per frame.
- [O4] LOW st_stuff.c — six pieces of write-only status-bar state including the
  whole status-bar chat cluster (chat lives in hu_lib), plus three struct fields
  never read.
- [O4] LOW five never-called helpers across m_menu/hu_lib/wi_stuff.
- [O4] LOW m_menu.c — M_ChangeDetail is a no-op that still owns F5 and writes to
  stderr; the Options row was removed but the keybind was not.
- INCIDENTAL: st_stuff.h declares ST_Responder twice.

## Lane 13 — platform layer and resource management

- [O1] HIGH Makefile — the -O0 finding, found independently. See the verdict.
- [O1] HIGH w_wad.c — EVERY LUMP LOOKUP BY NAME IS A LINEAR SCAN of the whole
  directory (~2300 entries for doom.wad), and W_CacheLumpName is a thin wrapper
  over it. 151 call sites across 21 files, and SEVERAL ARE PER-FRAME DRAW PATHS,
  not load paths — f_finale.c's tiled background erase, its END%i lookup every
  frame, 28 sites in m_menu.c, 30 in wi_stuff.c. Change: hash chain built once
  (Chocolate DOOM's W_InitLumpHash is the reference). Effort LARGER.
  Override order is what breaks, and it breaks SILENTLY as wrong art with a PWAD
  loaded. Prove it by asserting the new lookup equals the old scan for every
  name at startup under DEV.
  >>> This is the same root cause as lane 12's Load/Save finding. One fix, two
  >>> lanes' worth of symptoms.
- [O4] HIGH i_sound.c — ~150 lines of SNDSERV/SNDINTR scaffolding in a 975-line
  file, neither macro defined by any build. ONE REGION NO LONGER COMPILES: the
  itimer block writes `audio_fd` and `mixbuffer`, which DOOM-0047 removed, so
  enabling SNDINTR today is a compile error, not a restored feature. The
  "kept in case we need it" justification is already false.
  NOTE: the repo keeps sndserv/ as historical reference — that decision covers
  the standalone directory; this is dead scaffolding inside a LIVE file.
- [O1] MEDIUM s_sound.c — a full directory scan per sound storing a result
  NOTHING READS: S_sfx[].lumpnum is written and read only by its own guard,
  while the live audio path uses i_sound.c's own cache. Same for `usefulness`,
  whose purge policy is commented out.
- [O1] MEDIUM i_system.c — a fixed 6 MB zone heap whose only tuning knob is
  compiled out: the mb_used config entry sits inside `#ifdef SNDSERV`, which no
  build defines, so ~/.doomrc's mb_used is SILENTLY IGNORED. 6 MB is a 1997
  figure; contemporary ports default an order of magnitude higher. Measure zone
  purge misses first.
- [O1] LOW i_video.c — the Classic present loop indexes the source by multiply
  per pixel. At -O2 this is free; at the -O0 the project actually builds with,
  it is an imul per pixel across the whole frame.
- [O1] LOW i_sound.c — each converted sound chunk is allocated and copied twice.
  CAVEAT: SDL_mixer frees abuf with SDL_free, so the allocator must match — if
  that cannot be established from the headers, leave it alone.
- [O2] MEDIUM i_video.c — four debug-key handlers hand-roll the reporting helper
  that sits above them, and the drift is ALREADY USER-VISIBLE: DOOM-0275 is open
  because the user pressed one mid-play-test and got no on-screen feedback.
- [O2] MEDIUM w_wad.c — the WAD directory validation is written twice verbatim;
  the project already extracted the SIBLING per-lump check into wad_bounds.h, so
  this is the one left behind. The reload path is the one nobody exercises.
- [O2] MEDIUM i_video.c — the SDL teardown and the window creation each exist
  twice, though the comment says "Single source of truth so the Vulkan and
  Classic window paths can't drift (DOOM-0039)" — only the fullscreen decision
  was actually shared.
- [O2] LOW z_zone.c — the heap consistency check written three times; two of the
  three functions are dead.
- [O2] LOW z_zone.h — the zone magic is duplicated as a literal in the macro,
  which also repeats the guard the function then performs again (on every lump
  cache HIT). Keep the double check — it buys the FILE:LINE — but say so.
- [O2] LOW i_net.c — hand-rolled byte-swap macros shadow <arpa/inet.h>'s, kept
  only off-Windows, and swap UNCONDITIONALLY where real ntohl is identity on a
  big-endian host. No supported target is big-endian, so reuse, not a bug — yet.
- [O2] LOW mus2mid.c — five near-identical event writers (~80 lines for ~25).
  LANE'S OWN COUNTER-ARGUMENT, which may outweigh the win: this file is ported
  faithfully from Chocolate DOOM and restructuring makes upstream diffs harder
  to read, and upstream is where a MUS bug fix would come from.
- [O3] MEDIUM s_sound.c — S_SetMusicVolume sets the volume TWICE (driving it to
  full before the real value) and re-assigns state its callee already set,
  jumping straight past the carefully chosen DOOM-0047 ceiling.
- [O3] LOW s_sound.c — a loop in S_StopChannel whose result is discarded; the
  decrement it was meant to guard happens unconditionally.
- [O3] LOW i_sound.c — I_SetChannels repeats an allocation I_InitMusic already
  performed. Do not leave both.
- [O4] MEDIUM nine functions with no caller across z_zone/w_wad/i_system/i_net,
  plus two prototypes for functions that exist nowhere. W_Profile carries
  ~100 KB of BSS reachable from nothing — but it was deliberately BOUNDED rather
  than deleted on DOOM-0400 on the grounds that it is id's code, so deleting it
  now REVERSES that call and is the maintainer's to make.
  Also four EMPTY-BUT-CALLED stubs (I_StartFrame, I_UpdateNoBlit, I_UpdateSound,
  I_SubmitSound) whose "// er?" / "// what is this?" comments say the opposite of
  the truth and shaped how the audio path was understood for years.
- [O4] MEDIUM i_video.c — a TEMPORARY DIAGNOSTIC marked "REMOVE once DOOM-0267
  is closed"; DOOM-0267 is ✅ and user-confirmed, so the stated condition is met.
  It also consumes the `/` key and mis-signals (DOOM-0275 again).
- [O4] MEDIUM s_sound.c — ~100 lines of dead DOS-era scaffolding. NORM_VOLUME
  expands to an identifier that does not exist, so any future use is a compile
  error rather than a wrong value.
- [O4] LOW i_net.c — a one-shot debug print in the packet path that reads the
  datagram through an int* pun BEFORE any validation of it.
- [O4] LOW build-guard remnants of SGI/Solaris; <malloc.h> deprecated by glibc.

## Lane 14 — build system and developer tooling

- [O1] HIGH Makefile — the -O0 finding, found independently a third time.
- [O2] HIGH release.sh vs windows-build.sh — ~28 duplicated lines including a
  USER-FACING README text and a THREE-WAY copy of the runtime DLL list, and
  windows-build.sh's own header asks a human to keep them in step. The asymmetry
  gives it away: release.sh's LINUX leg just calls build-appimage.sh.
  Change: call the sibling script, as the Linux leg already does. Effort QUICK.
  This is the release path — the one where getting it wrong ships.
- [O1] MEDIUM ci-local.sh — the pre-push gate runs two throwaway containers per
  push, each doing its own apt-get update + install, and rebuilds the tree from
  cold every time. Change: ccache (QUICK) + an image tagged by a hash of
  ci-deps.txt (LARGER). CAVEAT: a cached image pins package versions, which is a
  real loss of the CI fidelity the script exists for.
- [O1] MEDIUM Makefile — every shader edit forces a recompile of r_vulkan.cpp,
  the single heaviest TU (~10k lines), because it #includes the .spv.h blobs
  directly. This is the inner loop of every look-tuning session. Change: an
  extern declaration header + a tiny embed TU. Effort LARGER.
- [O1] MEDIUM Makefile — `make test` re-runs all test binaries SERIALLY on every
  invocation (the COMPILE is incremental and parallel; the RUN is neither), and
  it is the first step of a release and of every pre-push gate. testing.md
  states the property; the Makefile's comment claims "the set scales without the
  build getting longer", which is true of compilation only.
  Change: xargs -P for the run (QUICK). Per-test stamps would change what
  testing.md documents — a decision, not an edit.
- [O1] MEDIUM windows-smoke.sh — the syntax sweep compiles 68 TUs ONE AT A TIME,
  serially, on every push. README quotes ~15 s; most is recoverable.
  The failure list must be a FILE, not a shell variable set in a subshell.
- [O2] MEDIUM windows-smoke.sh — the sweep HARD-CODES a copy of the Makefile's
  compiler flags AND THE COPY HAS ALREADY DRIFTED: it says -std=gnu++20 where
  the build says -std=c++23, and drops -Wall for C++. So the gate that tells CI
  "the tree compiles for Windows" does not check it with the flags the real
  build uses. Effort QUICK.
- [O2] MEDIUM Makefile — the glslc+xxd recipe written three times, so
  --target-env=vulkan1.2 (load-bearing for pathtrace.comp) lives in three
  places. Also: shaders/cursor.frag is a REAL source file but is appended to
  SHADER_HDRS alongside the two synthetic variants.
- [O2] MEDIUM ci-local.sh — the gate is a hand-written mirror of build.yml and
  only the apt list is actually shared; ci-deps.txt's header proves the pattern
  works. The header records what this drift already cost: a red CI on
  2026-08-12. Effort LARGER (touches the workflow).
- [O2] MEDIUM docs/standards/dependencies.md — version pins copied into prose,
  and ONE COPY IS ALREADY WRONG: the standard says the AppImage toolchain uses
  floating `continuous` tags "with no version to bump", while build-appimage.sh
  pins three tags with three recorded sha256s and its own comment says the
  rolling tag is what it used BEFORE. So a dependency sweep reading the standard
  is told there is nothing to bump when there are three pins. Effort QUICK.
  >>> Also a direct instance of this drive's own no-counts doc rule.
- [O2] MEDIUM scripts/ — the PWAD writer exists THREE times, the WAD directory
  reader THREE times, a minimal PNG writer twice, a fractal field twice, and
  sibling-import uses two different mechanisms. pbr_derive.py's own comment
  concluded this was "below the Rule-of-Three extraction bar" — with three of
  each in the same directory, the bar is met now.
  The WAD readers are the real risk: three independent parsers of the same
  on-disk format, in a tree whose review tail is about malformed-WAD handling.
  DO NOT fold the two fractal generators — their outputs are committed,
  signed-off assets and any RNG change alters the image.
- [O1] MEDIUM scripts/pbr_derive.py — the height field is recomputed THREE times
  per texture (derive_height, derive_normal, derive_ao all call it on identical
  input); a closure is defined INSIDE the per-pixel double loop; and hero_ao.py
  round-trips grayscale through a 3x RGB buffer so the same function can average
  it straight back. Also two dead parameters. Effort QUICK, outputs must be
  byte-identical (derived/ is gitignored so the check is free).
- [O4] MEDIUM scripts/make_bringup_hero.py — bring-up scaffolding the DOOM-0042
  plan explicitly scheduled for deletion, still present; five of its six outputs
  are reached by NOTHING. The trap: its ONE live consumer is
  tests/rb_image_test.cpp, so whoever finally runs the plan's `git rm -r` step
  breaks that test unless they notice. Effort QUICK.
- [O3] LOW packaging/linux-build.sh — the whole script is one exec of another,
  and NOTHING CALLS IT: README documents build-appimage.sh directly, release.sh
  calls it directly, and the only two references are prose. The asymmetry it
  claims to fix points the other way (windows-build.sh has 40 lines of logic).
- [O3] LOW ci-local.sh + Makefile — the `-j$(nproc)` passed to make is EITHER
  DEAD OR IT DEFEATS the Makefile's RAM cap, because the Makefile APPENDS its
  own -j to MAKEFLAGS after the command line is decoded. So the explicit-override
  route the Makefile's comment advertises does not work. Either way one of the
  two is wrong, and the container case matters most (a container's memory limit
  is not the host's MemAvailable). One @echo settles which.
- [O4] LOW Makefile — `clean` removes *.o/*~/*.flc at the top level where no
  build has ever written them (1997 Emacs artefacts); a one-test phony alias the
  generic rule already covers; -MMD -MP restated in TEST_CXXFLAGS.
- [O1] LOW Makefile + build-appimage.sh — the test link line omits $(LDFLAGS) so
  16 test links do not use mold though it was detected; and NO_STRIP=1 combined
  with -g and no -O ships the full debug info of every object, including the
  10k-line TU, as player download size. No comment says why stripping is off.
- LANE'S ANSWER on release.sh's stamping: nothing found. The two halves prove
  different links in the chain and neither implies the other, so removing either
  is a contract change, not a simplification.
- LANE'S NOTE on `make` not building tests: the property is documented loudly in
  CLAUDE.md but NOT AT THE POINT OF USE — neither the Makefile's own unit-test
  block nor testing.md carries the stale-binary warning CLAUDE.md calls "the
  most convincing false green there is".

## ORCHESTRATOR VERDICT — the -O0 build, measured

Three lanes (11 platform, 13 game-loop, 14 build) found this independently,
which is the strongest cross-cutting signal in the sweep. I verified the claim
and then measured it, because all three inferred a magnitude none had tested.

**CONFIRMED, by reading the flags the compiler actually received:** the shipped
engine compiles with `-g` and NO `-O` flag, i.e. -O0. Only TEST_CXXFLAGS adds
-O2. Every other flag choice in that Makefile carries an explaining comment;
this one does not, and the -O2 that WAS added went to the test binaries. It
looks inherited from id's 1997 Makefile and never revisited, not decided.

**MEASURED — and the lanes' magnitude claim does NOT hold up.** A/B on this
machine, full clean rebuild each way, Classic tier, six timedemo runs of the
walkuse fixture, comparing user CPU time (which excludes waiting):

    -O0   16.791 s user     -O2   15.981 s user      = 4.8% less CPU

A 12-map load-and-boot sweep was within noise either way (44.2 s vs 44.7 s
wall, 40.9 s vs 41.1 s user), because that workload is dominated by Vulkan
init rather than engine C.

So: the finding is REAL and worth fixing, but lane 14's "everything else is a
percentage of a frame; this is a multiple of one" is NOT supported by anything
measured. I could not reproduce a dramatic win.

**What my measurement does NOT settle, stated plainly.** My workload includes
per-run startup, so the render-loop-only share of that 4.8% is larger than
4.8% by an unknown factor. And I measured CLASSIC, which is not where the
project's frame budget is fought — the known CPU pole is the Solid/Ultra
per-frame build (lights, reheight, sprites), and reaching that needs the
project's own `\` CPU profiler on real hardware, which a headless session
cannot do. The honest summary is: real, safe, modest on the one tier I could
measure, unquantified on the tier that matters.

**SAFETY — measured, and this is the blocking question answered.** At -O2:
  - make test: 24 suites, all passed.
  - The five demo fixtures: 30 / 30 / 30 / 70 / 350, unchanged. That is the
    check that would catch any playsim divergence, and it did not move.
So -O2 does not break the simulation on this tree today. NOT yet checked at
-O2: the 68-map sweep, the Windows cross-build, and -shotcompare's golden gate
for Classic bit-identity.

**The flag is NOT changed in this branch.** Altering the optimisation level of
every binary the project ships is a decision for the maintainer, not a quick
fix I should take unilaterally — so it is filed, with the numbers above, rather
than applied. Two lanes also recommend pairing it with -fno-strict-aliasing,
and they are right to: this code type-puns routinely (w_wad.c's
`*(int *)lump_p->name`, i_net.c's `*(int*)&sw`), and -O2 turns strict aliasing
ON. That pairing is not optional.
