# Hardware checks left by the 2026-10-27 code review

The review in PR #1 (branch `claude/gallant-mayer-5vuzy4`) was done in a cloud
container with no GPU, no WAD and no gamepad. Everything there was built and
unit-tested (`make`, `make test`), but nothing was run. This file lists what
still has to be checked on the reference machine (RX 6600), so a local session
can pick it up and tick it off.

Run each step from `linuxdoom-1.10/` after `make && make test`. Check off each
item in the PR description as it is done.

## 1. Validation-layer run (open question: `rg16f` storage images)

**The question.** `shaders/svgf_composite.comp:42` writes and
`shaders/taau.comp:25` reads an `rg16f` storage image (the TAAU motion vector).
glslc emits the SPIR-V `StorageImageExtendedFormats` capability for that format.
`PickPhysicalAndDevice` in `r_vulkan.cpp` (the `VkPhysicalDeviceFeatures
enableBase` block, around line 1729) never enables
`shaderStorageImageExtendedFormats`. Whether Vulkan 1.2 requires that feature for
this capability is unclear from reading the spec.

**How to check.**
1. Install the Khronos validation layer (`vulkan-validationlayers` or the
   Vulkan SDK). The engine turns it on by itself when the layer is present
   (`HasInstanceLayer("VK_LAYER_KHRONOS_validation")`) and prints every message
   as `R_Vulkan [validation]: ...`.
2. Play a level in **Ultra with ray tracing on** and in **Solid with ray tracing
   on**, at **50 %** render scale so TAAU is in the path. Then do the same at 100 %.
   For example, `-warp 1 1`, walk around for a minute, open the menu, change
   level once.
3. To catch barrier mistakes too, also run once with synchronisation validation
   enabled:
   `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`.

**What to look for.**
- A message naming `StorageImageExtendedFormats` /
  `shaderStorageImageExtendedFormats`. If one appears, fix it next to the
  `shaderStorageImageArrayDynamicIndexing` request: when
  `have2.features.shaderStorageImageExtendedFormats` is set, set
  `enableBase.shaderStorageImageExtendedFormats = VK_TRUE`. If it is absent,
  fall back to a format that needs no feature, or turn TAAU off with a warning.
- Any `SYNC-HAZARD-*` message. This PR added a depth write-after-write
  dependency to the shared render pass (`CreateRenderPass`) and `HOST_READ`
  barriers before CPU readbacks (`RecordHostReadBarrier`). Both should be
  silent now. Anything still reported is a real finding.
- No messages at all means the question is closed and nothing needs to change.

## 2. REJECT index wrap in the traced cull (`pt_common.glsl`)

**The issue.** `shaders/pt_common.glsl` around line 659 computes the REJECT bit
index as `uint pnum = hitSec * numSectors + es;`. When `numSectors` exceeds
65535, the product passes 2^32 and wraps. A wrap in GLSL `uint` is defined and the
read stays inside the buffer, so the result is a wrong visibility bit, not a
crash. A REJECT lump that size is about 512 MB, so no real map comes close. The
CPU copies of the same cull were moved to 64 bits in this PR (`RejectHides` in
`r_vulkan.cpp`).

**How to check / fix.** The simplest safe fix is host-side: where `numSectors`
is filled into the push constants / UBO (the `numSectors` member near
`r_vulkan.cpp:1143`), pass 0 when `g.numSectors > 65535`. That turns the shader
cull off for such a map, which is exactly what it does today for a map without a
REJECT lump. Confirm on hardware that an ordinary map (E1M1) still culls: the
lit-sprite count and the look are unchanged. Then decide with the user whether
to land it.

## 3. TAAU look change (expected to be visible)

The fix in `shaders/taau.comp` reads history at the display pixel with the
jitter removed. Below 100 % render scale, **Ultra and Solid's ray-traced view
should look sharper, with nothing else changing**.

- Compare 50 % render scale before and after this PR, same spot, camera still,
  then moving. Look for sharper edges, and for no new ghosting or shimmer on
  movement.
- The `-shotcompare tests/goldens/e1m1_ultra_rt.png` gate (see
  `tests/goldens/README.md`) is likely to FAIL with this change, since the
  golden encodes the old, softer look. Do not re-bless it until the user has
  looked at the new image and agreed it is better.

## 4. Smoke checks for the engine and save-file fixes

These have unit tests where the logic allows. The rest needs a WAD.

- **Boot.** Boot DOOM and DOOM II in Classic, Solid and Ultra. Let the attract
  demos play: the demo header check changed, so the demos must still play in
  sync.
- **Savegame round-trip** (testing.md "Savegames"). The layout did not change,
  but `P_UnArchiveThinkers` now clears `tracer`, bounds `movedir`/`lastlook` and
  refuses a save with a bodiless in-game player. Save mid-fight with a revenant
  missile in flight (DOOM II MAP07 onward), load it, and confirm it loads and the
  missile flies straight. Then load an old save made before this PR.
- **idclev.** In DOOM II, `idclev05` warps and `idclev33` is ignored. In DOOM,
  `idclev13` warps. In a netgame it does nothing.
- **CPU use.** In Classic, CPU use should drop from a full core to a few
  percent (the `TryRunTics` sleep). Frame pacing should still be 35 FPS, with no
  stutter.
- **Window and gamepad.** Resize the window and toggle fullscreen in Classic
  with the menu open: the crisp menu text should stay sharp at the new size.
  With two pads connected, unplug the active one: the other should take over.
