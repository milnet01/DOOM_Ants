# Golden images (DOOM-0202 `-shotcompare` visual-regression gate)

Reference PNGs for the offscreen visual-regression gate. Each is a downscaled
(longest edge 640, box-filtered) capture of a fixed Ultra-RT camera view, so a
graphics change that breaks the *look* — a wrong composite, a broken tonemap, a
mis-placed sprite — is caught automatically instead of only by eyeballing.

## Run the gate

```sh
DOOMWADDIR=../wads ./linux/linuxxdoom -warp 1 1 \
    -shotcompare tests/goldens/e1m1_ultra_rt.png
```

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
