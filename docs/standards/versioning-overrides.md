# Versioning — DOOM_Ants's answers

Which version number a release gets is the machine-wide versioning standard,
`~/.claude/standards/versioning.md`: its levels, its `0.x` shift and its
`-rc.N` pre-release spelling. That standard asks each project two questions it
will not answer itself. This file answers them for DOOM_Ants, and says how the
roadmap's planned release headings follow the numbers. Decided by the user
2026-09-28.

## Breaking surfaces

A change is breaking when a player who upgrades has something that used to work
stop working. In DOOM_Ants that is, most often, a change to one of these:

- **Saved games.** A save that loaded in the previous release is refused or
  misread. The signature in `save_signature.h` refuses a save whose archived
  structs changed size; a change that keeps every size is misread instead.
  Either way, any change to an archived struct is breaking.
- **The settings file.** A `~/.doomrc` key is removed or renamed, or a value
  means something different, so a player's setting is lost or misread.
- **Command-line options.** An option is removed or renamed, or its meaning
  changes. Out of scope: options that only a developer build (`make DEV=1`)
  accepts, and the self-tests (`-rtverify`, `-shotverify`, `-shotcompare`,
  `-bootsmoke`).
- **Recorded demos.** A demo recorded on the previous release plays back
  differently. Treat any change to the game simulation as this, unless such a
  demo has been played back unchanged.
- **Game data.** A WAD or add-on that loaded before is refused.
- **Network play.** A way of starting or joining a network game stops working.
- **Default controls.** A key or gamepad button does something else by
  default.
- **What the game needs to run.** A machine that ran a render tier can no
  longer run it: a raised Vulkan or GPU-feature floor, or a raised operating
  system floor for the Linux or Windows download.

A surface missing from this list is still a surface (`versioning.md` § 3).

## What makes it 1.0

The release that leaves no planned or in-progress item under the roadmap's
`1.0.0` heading, or under any heading before it, is cut as `1.0.0`, whatever level its changes
would otherwise give. Until then every release is `0.x`.

## Planned release headings

The roadmap names its planned releases by version, but a release's number is
read from its changes when it is cut. So a planned `0.x` heading's number is a
queue label, not a promise:

- When a release is cut, its heading is renamed to the number it got.
- A later `0.x` heading keeps its label unless a cut takes that number. Then it,
  and every `0.x` heading after it, moves up one MINOR.
- The `1.0.0` heading is never renamed.
