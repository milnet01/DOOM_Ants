# Branding

The DOOM Ants wordmark, for places that show the project rather than run
it, such as the Ants Projects Hub landing page. Nothing in the engine or
its build reads these files.

| File | What it is |
|---|---|
| `doom-ants-logo.svg` | The wordmark. All shapes, no fonts, transparent background. Made for a dark background. |
| `doom-logo-source.svg` | The "DOOM" lettering it is built from, exactly as downloaded. |

## Provenance

- **"DOOM" lettering.** Wikimedia Commons, `File:Doom – Game's logo.svg`:
  https://commons.wikimedia.org/wiki/File:Doom_%E2%80%93_Game%E2%80%99s_logo.svg
  Tagged **PD-textlogo**: public domain, because it is simple text and
  shapes. Downloaded 2026-09-25. Recoloured here; the outline is unchanged.
- **Trademark.** DOOM is a trademark of id Software / Bethesda Softworks.
  Public domain covers copyright only. Using it here was the maintainer's
  decision (2026-09-25). Suggested credit: "DOOM is a trademark of
  id Software LLC, a ZeniMax Media company."
- **"ANTS" lettering and the ant.** Original to this project, drawn as
  plain polygons and ellipses, GPL v2 like the rest of the repo.

To make a PNG: `python3 -c "import cairosvg; cairosvg.svg2png(url='doom-ants-logo.svg', write_to='out.png', output_width=1600)"`.
