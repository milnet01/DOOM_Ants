# Versioning — DOOM_Ants's answers

Which version number a release gets is the machine-wide versioning standard,
`~/.claude/standards/versioning.md`: its levels, its `0.x` shift and its
`-rc.N` pre-release spelling. That standard asks each project two questions it
will not answer itself. This file answers them for DOOM_Ants, and holds nothing
else. Decided by the user 2026-09-28.

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
- **Recorded demos.** A demo that played back before now desyncs, which any
  change to the game simulation risks.
- **Game data.** A WAD or add-on that loaded before is refused.
- **Network play.** A way of starting or joining a network game stops working.
- **Default controls.** A key or gamepad button does something else by
  default.
- **What the game needs to run.** A machine that ran a render tier can no
  longer run it: a raised Vulkan or GPU-feature floor, or a raised operating
  system floor for the Linux or Windows download.

A surface missing from this list is still a surface (`versioning.md` § 3).

## What makes it 1.0

DOOM_Ants is `1.0.0` when every item under the roadmap's `1.0.0` heading, and
under every release heading before it, is shipped or dropped.

## Planned release headings

The roadmap names its planned releases by version. Under this rule the level
is read from the changes when the release is cut, so a planned heading's number
is its place in the queue, not a promise. When a release is cut at a different
number than its heading says, rename that heading to the number it got; the
later planned headings keep their themes and take the numbers after it.
