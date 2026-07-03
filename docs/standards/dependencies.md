# Dependencies Standard

Dependencies stay **current**. The default, always, is the latest stable
version — for new features *and* for security. A stale dependency is a bug
waiting to happen; an unpatched one is a security hole. Newer by default, and
you don't need a feature reason to bump — keeping up *is* the reason.

"Dependency" is broad: linked libraries (SDL2, Vulkan…), build tools (gcc,
g++, glslc, mold…), language runtimes/dialects, the CI workflow's pinned actions
and runner image, and the staged-library snapshot the Windows cross-build
carries. The main loci are enumerated under "Where this project's dependencies
live" below; the end-user *runtime* libraries have their canonical list in
`docs/RELEASE_README.txt`.

## The only exception

Hold an older version **only** when the latest one *explicitly breaks* a
feature and there is no reasonable way to move forward on the new version. A
hold is a last resort, never a default. "Haven't got round to bumping" is not
a hold — it's debt.

When you genuinely must hold, all three of these are mandatory:

1. **Pin it at the build site** (the Makefile, CI workflow, or staged prefix)
   with an inline comment naming the constraint, so the pin reads as
   *deliberate*, not neglected.
2. **Log it in the Version Exception Ledger below.** Not optional. An
   undocumented hold is indistinguishable from rot six months later.
3. **Name the first version that breaks us** — the re-test anchor. The Version
   Exception Ledger below defines how it's used (re-test when a newer version
   ships; drop the hold once it's fixed), so the rule isn't restated here.

A hold is **temporary by definition** — you expect some future version to fix
it. A constraint that will *never* be re-tested away — legacy code that needs an
old dialect, or a minimum version the code requires — is not a hold: record it
under **Permanent constraints** below, not in the ledger. When you're unsure
whether a newer version could ever clear the break, file it as a re-testable
hold — err toward re-testing.

## Version Exception Ledger

Temporary, re-testable holds live here — cases where a *future* version is
expected to fix the breakage. When a newer-than-broken version ships, re-test
the named feature; if it passes, bump it, delete the row, and say so in the
commit. If it still breaks, update "First broken version" to the newest version
you confirmed broken (so the next re-test starts above it). A row exists only
while the hold is active — resolving it means deleting the row, so there is no
separate status to track, and "First broken version" is the sole re-test anchor.

| Dependency | Held at | First broken version | What breaks (feature → symptom) | Recorded (ISO date) |
|------------|---------|-----------------------|----------------------------------|---------------------|
| _(none currently)_ | | | | |

No live holds today. A filled row looks like:

```
| libfoo | 2.3.1 | 2.4.0 | Ultra HD upscaler → libfoo 2.4 dropped the foo_scale() entry point the path calls, link fails | 2026-08-01 |
```

Re-test the moment a libfoo newer than 2.4.0 ships; delete the row once it works
again.

## Permanent constraints (not holds)

These version constraints are **not** temporary holds and do **not** go in the
ledger, because no version bump resolves them. Record them here so nobody
"downgrades to comply" or wastes a re-test on them. The two in this project
freeze opposite directions:

- a **cap** — never go *above* version X (a newer version breaks code that would
  have to be rewritten to move); and
- a **floor** — never drop *below* version X (the code requires at least X).

(A pin made purely for reproducibility — freeze at *exactly* version X, with
nothing broken — is the same kind of permanent, non-re-testable constraint;
record it here too, with its reason. This project has none today.)

Which direction each is matters, so it's named per entry:

- **Cap — C engine `-std=gnu11`** (Makefile:10-12,27). The 1997 engine's
  action-function table in `info.c` uses K&R empty `()` = *unspecified args*.
  C23 — which recent gcc selects by default, so the `-std=gnu11` pin is what
  keeps the engine on the old rules — makes empty `()` mean *no args* and rejects
  the table's function-pointer assignments, so the C engine won't compile under
  it. Clearing it means rewriting `info.c`'s idiom, not waiting for a newer gcc,
  so there is nothing to re-test.
- **Floor — shader target `--target-env=vulkan1.2`** (Makefile:225-228). The
  *minimum* SPIR-V/Vulkan level `pathtrace.comp` needs (`VK_KHR_ray_query` +
  `buffer_reference` want SPIR-V 1.5). A newer target env is acceptable; older is
  not — a lower bound we require, not a version we're stuck under.

## Where this project's dependencies live

There is no package-manager lockfile — this is a Makefile C/C++ project on
system libraries. The dependency surface is (the legacy `sndserv/` sound-server
sub-Makefile is not built by CI or the default `make`, so it's out of scope):

- **Linked libraries**, resolved by `pkg-config` at build time: SDL2, SDL2_mixer,
  and the Vulkan loader (Makefile:15-20, 35 — Vulkan falls back to `-lvulkan` if
  pkg-config lacks it, Makefile:20). Kept current automatically by the rolling
  distro (openSUSE Tumbleweed) — so "check for updates" here means a system
  update, and the pins to watch are ours, not theirs.
- **Runtime libraries (music):** SDL2_mixer renders the in-WAD music through
  **FluidSynth** + a General-MIDI soundfont — a shared-library dependency
  SDL2_mixer pulls in, not something the engine links itself (see
  docs/specs/DOOM-0016-music.md). These plus the full end-user runtime set are
  the canonical list in `docs/RELEASE_README.txt:18-33`; keep that list current
  with the rest.
- **Build tools:** gcc, g++, `glslc` (shaderc), `xxd`, `mold` (optional faster
  linker), `pkg-config`, `make`.
- **Language dialects:** C engine `-std=gnu11` (a frozen dialect — see Permanent
  constraints); C++ renderer back-end `-std=c++17` (Makefile:28-29). C++17 has no
  recorded breakage, so under this standard it is a bump candidate whenever a
  sweep revisits it — moving it forward is allowed and encouraged, using current
  idioms.
- **Windows cross-build:** `mingw64-cross-gcc`/`-c++` (Makefile:191-192) plus
  SDL2 / SDL2_mixer / Vulkan dev libs staged under `../mingw-deps/prefix`
  (Makefile:198-209). The staged versions are pinned in `mingw-deps/README.md:19-20`
  (currently SDL2 2.32.10, SDL2_mixer 2.8.2, Vulkan-Headers 1.4.350.0) — a *manual
  snapshot*, the one place a version can silently go stale, so it gets the closest
  watch on the sweep.
- **CI (GitHub Actions):** `.github/workflows/build.yml` pins the action
  `actions/checkout@v4` and runs on the `ubuntu-latest` runner image. Its Linux
  build-deps are the single-source-of-truth apt list `packaging/ci-deps.txt`
  (`build-essential`, `libsdl2-dev`, `libsdl2-mixer-dev`, `libvulkan-dev`,
  `glslc`, `xxd`), shared with the local mirror `packaging/ci-local.sh`. Two
  version pins here are ours to keep current: the `actions/checkout@vN` action,
  and the container image `packaging/ci-local.sh` hardcodes to mirror the runner
  (`CI_IMAGE="docker.io/library/ubuntu:24.04"`, ci-local.sh:35). See the sweep
  posture below for the lockstep rule between them and `ubuntu-latest`.
- **Release packaging (AppImage):** `packaging/build-appimage.sh` (driven by
  `packaging/release.sh`) bundles the runtime libs (SDL2, SDL2_mixer, FluidSynth
  + a GM soundfont) and fetches three tools — `linuxdeploy`, `appimagetool`,
  `type2-runtime` — from their `continuous` release tags (build-appimage.sh:34-36).
  Those `continuous` tags float like `ubuntu-latest` (no version to bump); the
  bundled libs ride whatever the build host provides.

## Staying current (sweep posture)

Check, don't wait for a break:

- **On dep-adjacent work:** whenever you touch the Makefile or `mingw-deps/` for
  any reason, glance at the versions on the way past.
- **At the start of a release cycle:** check what's behind — `pkg-config
  --modversion <lib>`, the distro's update channel, and the upstream releases
  of the mingw-staged libraries.
- **Check the CI pins.** Every `uses: <action>@vN` in
  `.github/workflows/build.yml` is a version pin this rule governs — glance at
  them on the release-cycle sweep. GitHub deprecates the action runtime on a
  cadence, so a lagging `@vN` warns today and hard-fails later. `ubuntu-latest`
  floats by design, but the mirror image `packaging/ci-local.sh` pins
  (`ubuntu:24.04`) does **not** — bump it in lockstep when `ubuntu-latest`
  advances.
- **Bump and idiom-refresh ship together.** When you move a dependency forward,
  update the code that calls it to the new version's current idioms *in the same
  change* — never bump the library and leave stale-API calls behind. Otherwise
  the cleanup never happens and the codebase rots into a museum of "compiles,
  but nobody meant it to."
- **Re-test the ledger.** Any ledger hold whose "First broken version" is now
  older than the latest available version is due a re-test — see above.
  Permanent constraints are exempt: there is nothing to re-test.

## Security

Security rides on the latest-stable default above — no separate track, and you
never wait for a feature reason to take a security patch. The one point worth
stating on its own: a known-vulnerable pinned version is itself a defect. If a
documented hold ever traps a dependency
on a version with a security advisory, that is a **priority to resolve** (find a
way forward on the patched version, or carry a local patch) — never something to
sit on because the ledger row exists.
