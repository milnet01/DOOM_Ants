# Golden images (DOOM-0202 `-shotcompare` visual-regression gate)

Reference PNGs for the offscreen visual-regression gate. Each is a downscaled
(longest edge 640, box-filtered) capture of a fixed Ultra-RT camera view, so a
graphics change that breaks the *look* — a wrong composite, a broken tonemap, a
mis-placed sprite — is caught automatically instead of only by eyeballing.

## Run the gate

Run it from `linuxdoom-1.10/`, on a private display, so no window opens on the
desktop:

```sh
printf 'renderer 1\n' > "$CFG"    # a fresh file outside the repo, every run
xwfb-run -c cage -s '\-geometry' -s 1920x1080 -- \
  env WAYLAND_DISPLAY=doom-ab-no-wayland SDL_VIDEODRIVER=x11 \
      DOOMWADDIR="$PWD/../wads" DOOMASSETDIR="$PWD/../assets/ultra/" \
  timeout 80 ./linux/linuxxdoom -iwad "$PWD/../wads/doom.wad" -config "$CFG" \
    -warp 1 1 -noinput -shotcompare tests/goldens/e1m1_ultra_rt.png
```

- The config must say `renderer 1`. The gate does not pick the tier, and a run in
  Classic printed no result until the timeout stopped it (2026-11-20). The engine
  rewrites the config on exit, so write it fresh each time.
- The golden was captured on that private display. The capture takes the shape
  of the engine's window, so a run in a window of another shape fails with
  `size mismatch`. A full-screen 16:9 desktop is one such shape.

The engine renders the static spawn view (45 warm-up frames so SVGF settles),
downscales the capture the same way, and compares it to the golden by
mean-absolute-error over RGB. Exit code: **0 = PASS**, **3 = FAIL** (MAE over
threshold or size mismatch), **1 = I/O error**. On this hardware the RT capture
is bit-exact, so an unchanged view scores `mae=0.000`.

## Bless / re-bless a golden

`-shotcompare <path>` **bootstraps** the golden when `<path>` doesn't exist yet:
it writes the current capture there and exits 0. So to re-bless after an
*intentional* look change, delete the stale golden and re-run — the next run
writes the new reference. Commit the PNG (they are ~200 KB downscaled, small
enough for plain git — no LFS).

## Caveats

- Ultra-RT only today (the raster/Solid + Classic tiers have no TRANSFER_SRC
  capture image yet — a DOOM-0202 follow-up).
- Needs a GPU, so this is a local / self-hosted-runner gate, not a
  GitHub-hosted-runner one (see DOOM-0203).
- The golden encodes the current look; a deliberate visual change is *expected*
  to fail the gate until re-blessed.
