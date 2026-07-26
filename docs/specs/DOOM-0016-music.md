# DOOM-0016 — General-MIDI music playback

**Status:** Implemented (2026-06-12) — user-signed-off; shipped in i_sound.c + mus2mid.c
**Kind:** feature
**Depends on:** DOOM-0004 (SDL2 audio layer), shipped.

## Goal

Make DOOM's music play. The game ships its soundtrack inside the WAD — title
screen, every level, the intermission and victory screens — but since the SDL2
port (DOOM-0004) the music code has been empty stubs, so the game has been
silent except for sound effects. This feature wires music up as clean,
full-quality **General MIDI** (a smooth, modern "good sound-card" rendering),
while leaving the working sound effects completely untouched.

For a player: boot a level and you hear the music, exactly as the original did.

## Background — why it doesn't "just work"

Two facts about the existing code drive the whole design:

1. **DOOM stores music in MUS format.** MUS is a compact 1990s cousin of MIDI.
   No modern audio library plays it directly — it must be converted to standard
   MIDI first.

2. **The sound effects own the audio device at 11025 Hz.** `i_sound.c` runs a
   hand-rolled software mixer (`I_SDLAudioCallback` → `I_MixSound`) on a legacy
   `SDL_OpenAudio` device locked to 11025 Hz. That mixer's pitch maths assume
   that rate, so we cannot change it without breaking effects. Music routed
   through that same low rate would sound muffled — fighting the "clean" goal.

## Approach

Give music its **own** audio output. Open a *second* device through
**SDL2_mixer** (a higher-level audio library) at full 44100 Hz, independent of
the effects device. SDL2_mixer renders MIDI through **FluidSynth** (a software
synthesizer) using the **FluidR3_GM** soundfont (an instrument bank) already
installed on the system. The operating system mixes the two output streams.

This was chosen for **lowest regression risk**: the shipped, player-verified
effects mixer is not touched at all. Music lives in a parallel code path.

### Alternative considered — unify everything on SDL2_mixer

Retire the hand-rolled effects mixer and run *both* effects and music through
SDL2_mixer at 44 kHz. This is the tidier long-term architecture and deletes
~200 lines of custom mixing, but it reworks just-shipped, verified effects code
(DOOM-0004/0005), so it carries real regression risk for a feature the player
already confirmed works. Rejected for now in favour of the zero-risk parallel
path; unifying can be revisited later as its own refactor item if desired.

A third option — bolting music onto the existing 11025 Hz mixer — was rejected
because it locks music to a muffled low quality, contradicting the chosen
"clean General MIDI" sound.

### Verified assumptions

These were proved with throwaway test programs against the installed libraries
before this spec was written (not assumed):

- The legacy `SDL_OpenAudio` effects device and a second `Mix_OpenAudioDevice`
  music device **coexist** in one process.
- `Mix_Init(MIX_INIT_MID)` is **required** — without it, SDL2_mixer exposes no
  MIDI decoder (only `CMD WAVE`). With it, `FLUIDSYNTH TIMIDITY MIDI` appear.
- `Mix_SetSoundFonts("/usr/share/sounds/sf2/FluidR3_GM.sf2")` is accepted.

Installed and confirmed: SDL2_mixer 2.8.2 linked against `libfluidsynth.so.3`;
soundfont present at `/usr/share/sounds/sf2/FluidR3_GM.sf2`.

## The game-side contract (unchanged)

The game calls a fixed "music API" in `i_sound.c`, and `s_sound.c` already calls
it correctly. **No game code (`s_sound.c`, `d_main.c`, …) is modified** — only
the platform implementations of these functions change:

| Function | Today | After |
|----------|-------|-------|
| `I_InitMusic` | empty | init SDL2_mixer + open music device + set soundfont |
| `I_RegisterSong(data, length)` | returns 1 | convert MUS→MIDI (reads bounded by `length`), load as a `Mix_Music`, return handle |
| `I_PlaySong(h, looping)` | no-op | `Mix_PlayMusic` (loop forever when `looping`) |
| `I_PauseSong` / `I_ResumeSong` | no-op | `Mix_PauseMusic` / `Mix_ResumeMusic` |
| `I_StopSong` | no-op | `Mix_HaltMusic` |
| `I_SetMusicVolume(v)` | stores var | `Mix_VolumeMusic(clamp((v * 128) / 15, 0, 128))` (`v` is 0–15) |
| `I_UnRegisterSong(h)` | no-op | `Mix_FreeMusic` + free the song's MIDI buffer |
| `I_ShutdownMusic` | empty | halt, close music device, `Mix_Quit` |

`I_RegisterSong(data, length)` receives the lump pointer **and its real byte
length** (`W_LumpLength`). The MUS header also carries its own declared size
(score start offset + score length); the converter bounds every read by the
smaller of the two, so a crafted/truncated lump that inflates its header length
cannot read past the buffer (DOOM-0093, indie-review 2026-07-23). (Call site:
`s_sound.c` → `music->data = W_CacheLumpNum(...)`;
`I_RegisterSong(music->data, W_LumpLength(music->lumpnum))`.)

The volume `v` reaching `I_SetMusicVolume` is DOOM's 0–15 scale: the music
slider is capped at 15 (`m_menu.c:842`) and passed straight through
(`m_menu.c:847`), with `snd_MusicVolume` defaulting to 15 in code
(`s_sound.c:116`) and 8 from the config file (`m_misc.c:239`) — both within
0–15. (The `volume > 127`
guard in `S_SetMusicVolume`, `s_sound.c:618`, is only a sanity ceiling, not the
operating range — the live values are 0–15.) `S_SetMusicVolume` also makes a
stray `I_SetMusicVolume(127)` call immediately before the real one
(`s_sound.c:624–625`); the clamp to 0–128 makes that harmless.

There is also a ninth music function, `I_QrySongPlaying`. It was dead when this
spec was written (its only caller was commented out), and the plan was to delete
it. **Superseded:** DOOM-0165 (silent title music on first launch) gave it a real
implementation and a live caller — `S_StartMusicInfo` uses it to verify that a
song actually started and to drive the retry. It is part of the live contract
now; `looping` and `musicdies` stay with it.

## Components / affected files

1. **`Makefile`** — add `SDL2_mixer` to the existing pkg-config `--cflags` and
   `--libs` invocations (mirroring the current `sdl2` lines), **and** add
   `$(O)/mus2mid.o` to the hand-maintained `OBJS` list (an explicit list with no
   wildcard, starting line 24) so the new converter is compiled and linked — the
   generic `.c.o` suffix rule handles the compile. Three one-line edits in total.

2. **New `linuxdoom-1.10/mus2mid.c` + `mus2mid.h`** — MUS→MIDI converter ported
   from Chocolate DOOM (GPL v2, licence-compatible; credit retained in the file
   header). Input: MUS byte buffer. Output: a heap-allocated MIDI byte buffer +
   its length. Includes a small (~30-line) growable byte buffer so we don't also
   port Chocolate DOOM's larger `memio` helper.

3. **`linuxdoom-1.10/i_sound.c`** — replace the eight dummy music functions
   (`I_SetMusicVolume` at 409; `I_InitMusic`/`I_ShutdownMusic`/`I_PlaySong`/
   `I_PauseSong`/`I_ResumeSong`/`I_StopSong`/`I_UnRegisterSong`/`I_RegisterSong`
   across 820–866) with the SDL2_mixer-backed versions above, and **remove** the
   dead ninth function `I_QrySongPlaying` (869–874) along with the now-unused
   `looping`/`musicdies` statics (823–824). Add the
   `#include <SDL2/SDL_mixer.h>` and a small handle table mapping a returned
   handle to its `{Mix_Music*, midi_buffer}` so `I_UnRegisterSong` frees both.
   The converted MIDI buffer is kept alive until unregister (defensive: do not
   assume `Mix_LoadMUS_RW` copies it).

   **Implementation note (found during build):** stock linuxdoom never actually
   *called* `I_InitMusic` (only `I_ShutdownMusic` is wired, from `i_system.c`),
   so music would have stayed silent even with real code behind the API. The
   fix adds a single `I_InitMusic();` call at the end of `I_InitSound()` — still
   inside the platform sound file `i_sound.c`, so the "no game code (`s_sound.c`,
   `d_main.c`) changes" guarantee holds.

### Soundfont selection

`I_InitMusic` picks the soundfont in this order:

1. `$DOOM_SOUNDFONT` if set and the file exists.
2. `/usr/share/sounds/sf2/FluidR3_GM.sf2` if it exists.
3. Otherwise call no `Mix_SetSoundFonts`; SDL2_mixer then tries its built-in
   default backend (timidity, if a usable patch/config set is present — this is
   best-effort, not guaranteed to produce audio). If nothing can render the
   MIDI, music is silently skipped and the game runs normally (see Failure
   handling).

This also sets up the future Windows build (DOOM-0006) to point the env var at a
bundled soundfont.

### Failure handling

Music is **non-essential**. If `Mix_Init`, the device open, or song loading
fails, log a one-line warning to stderr, disable music, and continue. The game
must still boot and play effects with no music — never `I_Error` out over music.

## Verification

- **Effects unchanged:** play the game; gunshots, doors, pickups sound exactly
  as they do now. (No effects code was touched — this is the core safety claim.)
- **Music plays & loops:** boot a level, hear its track; let it run past the end
  to confirm it loops.
- **Volume control:** the menu's music-volume slider changes loudness; setting
  it to zero silences music. (This is the only disable path — the engine has no
  `-nomusic`/`-nosound` command-line switch today, and adding one is out of
  scope for this item.)
- **Graceful degrade:** temporarily move the soundfont aside → the game still
  boots and plays effects, just without music; a warning is logged.
- **Build:** `make` links cleanly with `SDL2_mixer`; no new warnings.

## Out of scope (YAGNI)

- OPL2/AdLib (gritty FM-synth) playback — explicitly not chosen.
- Music formats beyond MUS/MIDI; external music packs or replacement tracks.
- Playing more than one song at once (DOOM never does).
- Unifying effects onto SDL2_mixer (possible later refactor, not this item).

## Cold-eyes loop log

- **2026-06-12 — 5 loops to clean** — Independent cold reviewer per loop, briefed against current source (no prior-loop context). L1: HIGH — verification step referenced non-existent -nomusic/-nosound switches (removed); MED — 9th dead fn I_QrySongPlaying + looping/musicdies statics unaccounted (added). L2: HIGH (dismissed — reviewer misread the 127 I_Error sanity-ceiling as the operating range; verified live domain is 0–15 via m_menu.c:842; added sourced clarification); MED — ~408→409 cite; MED — "remove" vs "replace" for I_QrySongPlaying reconciled. L3: HIGH — Makefile OBJS is an explicit hand-list, mus2mid.o must be added (was build-breaking; fixed, edit-count→3); MED — config default 8 (m_misc.c:239) vs code 15 clarified; LOW — 816→820 range. L4: HIGH — spec/ROADMAP Makefile edit-count drift (ROADMAP 2→3); MED — folded clamp into the volume formula; MED — softened unverified "timidity fallback produces audio" claim to best-effort. L5: 0 CRITICAL / 0 HIGH; 100% citation accuracy verified; one clarity nit (annotate v is 0–15) applied. Converged.
