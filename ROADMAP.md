# Roadmap

The plan for DOOM_Ants, in three phases. Every actionable item carries a
permanent `[DOOM-NNNN]` ID (append-only — never renumbered, never reused).
Status: 📋 planned · 🚧 in progress · ✅ shipped · 💭 considered.

See `docs/standards/roadmap-format.md` for the format. Shipped items graduate
into `CHANGELOG.md`.

## Phase 0 — Foundations

Documentation, standards, and getting the project published. The groundwork
that everything else builds on.

- 🚧 [DOOM-0001] **Establish the documentation & standards tree.**
  **Layman:** Set up the project's rulebooks and roadmap so the work stays organised and unambiguous.
  Kind: doc.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0002] **Publish DOOM_Ants as a public GitHub repository.**
  **Layman:** Put the project online, publicly, so it can be shared and downloaded.
  Kind: chore.
  Source: in-session-2026-06-11.

## Phase 1 — Build, Modernise & Share

Get the 1997 engine compiling, running, and playable on a modern machine —
without changing how it plays. The SDL2 layer makes this cross-platform, so
this phase also delivers a **Windows** build and downloadable releases to share
with friends.

- 📋 [DOOM-0003] **Get linuxdoom-1.10 compiling on modern 64-bit Linux.**
  **Layman:** Fix the 1997 code so today's compiler can build it.
  Kind: fix.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0004] **Replace legacy X11 video & sound with SDL2.**
  **Layman:** Swap the ancient display/sound code for a modern, cross-platform layer.
  Kind: refactor.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0005] **Boot with a shareware WAD and confirm gameplay.**
  **Layman:** Actually run it with DOOM's data and check it plays like the original.
  Kind: test.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0006] **Add a Windows build target.**
  **Layman:** Produce a version that runs on Windows so friends can play it.
  Kind: feature.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0007] **Publish downloadable Linux & Windows builds via GitHub Releases.**
  **Layman:** Put ready-to-run downloads online so anyone can grab a copy.
  Kind: release.
  Source: in-session-2026-06-11.

## Phase 2 — The Spin

The creative overhaul: evolve the renderer toward true 3D with ray tracing and
modern lighting, holding 60 FPS, while keeping the original DOOM feel. These are
parked ideas (💭 considered) until we commit to and design each one.

- 💭 [DOOM-0008] **Convert the renderer to true 3D.**
  **Layman:** Replace DOOM's fake-3D trick with a real 3D engine.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0009] **Add hardware ray tracing (path tracing where feasible).**
  **Layman:** Use the graphics card to trace real light rays for accurate reflections and shadows.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0010] **Add dynamic lighting.**
  **Layman:** Let lights move and react — muzzle flashes, flickering lamps — lighting the scene live.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0011] **Add volumetric lighting (god rays).**
  **Layman:** Visible shafts of light through smoke and doorways.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0012] **Hold a 60 FPS performance floor.**
  **Layman:** Keep it running smoothly — never below 60 frames per second.
  Kind: perf.
  Source: in-session-2026-06-11.
