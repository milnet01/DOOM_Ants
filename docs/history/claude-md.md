# CLAUDE.md — history

Why some rules in `CLAUDE.md` read the way they do. Nothing here is a rule.
`CLAUDE.md` states what is true now; this file states how it came to say so.

## Render tiers

The tier definitions were set by the user on 2026-07-27. They revised an
earlier position in which Solid was "the same renderer as Ultra, minus the
HD art".

Under the earlier wording, effects were a property of the tier. Under the
current one they are not: the art is what separates the tiers, and the
ray-traced view is a separate axis. The two consequences stated in
`CLAUDE.md` — that effects are not a tier ladder, and that performance is
Solid's feature — follow from that revision and were easy to get backwards
while both wordings were in circulation.

## The unbuilt subdirectories

`sndserv/`, `sersrc/` and `ipx/` are kept as reference per DOOM-0085.

DOOM-0312 removed them from `source_roots` in `.ants/project.json`, so the
static-analysis sweeps stopped parsing them. That file's `_comment` records
the reason.

The findings already reported in them are recorded on DOOM-0414 and are not
fixed. The argument for leaving them: nothing builds those directories, so
no finding in them can reach a player.

## The stale release binary

`release.sh` once reused any artifact whose filename matched, so a release
could publish a binary built from an earlier commit.

It now stamps each build with the commit it came from and re-checks the
published assets. The releases standard's "Cutting a release" section owns
the current procedure and what to do when an asset is wrong.

## The wall-blooming raster view

DOOM-0331 INV-4 shipped a raster view in which every wall bloomed. A shader
constant scaled a value before a threshold compared it, which divides the
threshold. No check that reads the threshold's own table can see that.

The trap itself stays in `CLAUDE.md`, because it is true now and will catch
the next instance. The spec's INV-4 and INV-9 amendments carry the full
story.
