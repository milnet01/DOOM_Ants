# Dependencies Standard

Dependencies stay **current**. The default, always, is the latest stable
version — for new features *and* for security. A stale dependency is a bug
waiting to happen; an unpatched one is a security hole. Newer by default, and
you don't need a feature reason to bump — keeping up *is* the reason.

"Dependency" is broad: linked libraries (SDL2, Vulkan…), build tools (gcc,
g++, glslc, mold…), language runtimes/dialects, CI actions, container base
images, and any lockfile or staged-library snapshot.

## The only exception

Hold an older version **only** when the latest one *explicitly breaks* a
feature and there is no reasonable way to move forward on the new version. A
hold is a last resort, never a default. "Haven't got round to bumping" is not
a hold — it's debt.

When you genuinely must hold, all three of these are mandatory:

1. **Pin it at the build site** (Makefile, package manifest, staged prefix)
   with an inline comment naming the constraint, so the pin reads as
   *deliberate*, not neglected.
2. **Log it in the Version Exception Ledger below.** Not optional. An
   undocumented hold is indistinguishable from rot six months later.
3. **Name the first version that breaks us**, so a future session knows
   exactly when to re-test: the moment a version *newer than that* ships, the
   feature is re-checked and, if it's fixed, the hold is dropped.

## Version Exception Ledger

Every held-back dependency lives here. When a newer-than-broken version ships,
re-test the named feature; if it passes, bump it, delete the row, and say so in
the commit. If it still breaks, update "First broken version" to the newest
version you confirmed broken (so the next re-test starts above it).

| Dependency | Held at | First broken version | What breaks (feature → symptom) | Recorded | Re-test trigger | Status |
|------------|---------|-----------------------|----------------------------------|----------|-----------------|--------|
| C language dialect (engine) | `-std=gnu11` (Makefile:10-12,27) | C23 (gcc's current default) | The 1997 engine's action-function table in `info.c` uses K&R empty `()` = *unspecified args*. C23 makes empty `()` mean *no args* and rejects the table's function-pointer assignments → the C engine won't compile. | 2026-07-03 | **Permanent by design.** This is legacy 1997 C, not an upstream bug that will be "fixed" — clearing it would mean rewriting `info.c`'s idiom, not waiting for a new gcc. Revisit only if that rewrite is ever undertaken. | HELD |

### Deliberate floors (minimums, *not* holds)

These are lower bounds we require, not caps we're stuck under — they don't
belong in the ledger, but note them so nobody "downgrades to comply":

- **Shader target `--target-env=vulkan1.2`** (Makefile:225-228) — the *minimum*
  SPIR-V/Vulkan level `pathtrace.comp` needs (`VK_KHR_ray_query` +
  `buffer_reference` want SPIR-V 1.5). A newer target env is fine; older is not.

## Where this project's dependencies live

There is no package-manager lockfile — this is a Makefile C/C++ project on
system libraries. The dependency surface is:

- **Linked libraries**, resolved by `pkg-config` at build time: SDL2, SDL2_mixer
  (which transitively pulls opus and friends), and the Vulkan loader
  (Makefile:15-20, 35). Kept current automatically by the rolling distro
  (openSUSE Tumbleweed) — so "check for updates" here means a system update,
  and the pins to watch are ours, not theirs.
- **Build tools:** gcc, g++, `glslc` (shaderc), `xxd`, `mold` (optional faster
  linker), `pkg-config`, `make`.
- **Language dialects:** C engine `-std=gnu11` (held — see ledger); C++ renderer
  back-end `-std=c++17` (Makefile:27-29). C++17 has no recorded breakage, so
  under this standard it is a bump candidate whenever a sweep revisits it —
  moving it forward is allowed and encouraged, using current idioms.
- **Windows cross-build:** `mingw64-cross-gcc`/`-c++` plus SDL2 / SDL2_mixer /
  Vulkan dev libs staged under `../mingw-deps/prefix` (Makefile:198-209,
  `mingw-deps/README.md`). These are a *manual snapshot* — the one place a
  version can silently go stale — so they get the closest watch on the sweep.

## Staying current (sweep posture)

Check, don't wait for a break:

- **On dep-adjacent work:** whenever you touch the Makefile or `mingw-deps/` for
  any reason, glance at the versions on the way past.
- **At the start of a release cycle:** check what's behind — `pkg-config
  --modversion <lib>`, the distro's update channel, and the upstream releases
  of the mingw-staged libraries.
- **Bump and idiom-refresh ship together.** When you move a dependency forward,
  update the code that calls it to the new version's current idioms *in the same
  change* — never bump the library and leave stale-API calls behind. (Global
  rule 5b: otherwise the cleanup never happens and the codebase rots into a
  museum of "compiles, but nobody meant it to.")
- **Re-test the ledger.** Any held item whose "First broken version" is now
  older than the latest available version is due a re-test — see above.

## Security

Security is not a separate track — "latest stable" already covers it, and you
never wait for a feature reason to take a security patch. A known-vulnerable
pinned version is itself a defect. If a documented hold ever traps a dependency
on a version with a security advisory, that is a **priority to resolve** (find a
way forward on the patched version, or carry a local patch) — never something to
sit on because the ledger row exists.
