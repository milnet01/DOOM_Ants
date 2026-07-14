# DOOM-0042 — Ultra HD PBR Materials (E1M1 first slice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the full HD-PBR material pipeline end-to-end on one map (E1M1) in Ultra's ray-traced view — walls and flats gain albedo/normal/AO/emissive/POM from a sidecar-driven material set, with paletted fallback everywhere else and byte-identical Solid/Classic.

**Architecture:** Four layers, each independently buildable. **(A)** an offline asset pipeline — a repo-tracked `materials.csv` sidecar + an offline `scripts/pbr_derive.py` generator + hand-picked CC0 "hero" PNGs. **(B)** engine load/upload — a vendored `stb_image` decoder, a parallel bindless RGBA8 PBR image array beside the existing R8 paletted array, and a per-material control SSBO. **(C)** shading — a `usePBR` branch in `pathtrace.comp` doing RGBA albedo, normal mapping, ambient AO, primary-hit emissive, and parallax occlusion mapping. **(D)** a tier hook gating the whole path on `rendermode == RB_RT3D`.

**Tech Stack:** C (1997 engine, `-std=gnu11`), C++23 (Vulkan back-end), GLSL (compute path tracer, `glslc` → SPIR-V → embedded header), Python 3.10+ (offline generator, not in the engine build), Vulkan 1.2 + `VK_KHR_ray_query`.

## Global Constraints

Every task's requirements implicitly include this section. Values are copied verbatim from `docs/specs/DOOM-0042-ultra-hd-pbr-materials.md` (Approved) and ADR `docs/decisions/0002-ultra-material-sidecar-and-loader.md` (Accepted).

- **Tier gate:** the entire HD path is active only when `rendermode == RB_RT3D` (Ultra) **and** the ray-traced view (`rb_rtdebug == 6`, Ultra's default). Solid (`RB_RASTER3D`) and Classic (`RB_CLASSIC`) stay byte-for-byte unchanged — frame-diff must be 0.
- **Licence (HARD):** CC0 / free assets only. **No proprietary art, and no WAD-derived art, is committed to the repo.** Derived PNGs live under `assets/ultra/derived/` and are **gitignored** exactly like the WAD. Every hero material's provenance is recorded in `assets/ultra/LICENSES`.
- **Control SSBO layout:** `std430`. Struct per material id (same count/order as `RB_MaterialCount()` = walls, then flats, then sprites): `int maps[7]` in CSV map-column order `[0]albedo [1]normal [2]roughness [3]metallic [4]ao [5]emissive [6]height` (`-1` = no map); `float uvScale`; `uint flags` (bit0 `pom`, bit1 `noPom`, bit2 `sprite`); `uint usePBR` (0 = paletted fallback). **v1 uploads only 5 maps** (albedo, normal, ao, emissive, height); `maps[2]`/`maps[3]` (roughness/metallic) stay `-1` — DOOM-0103 adds their upload+sample later.
- **Colour space:** albedo + emissive sampled as **sRGB** (decode to linear); normal/roughness/metallic/ao/height sampled **linear**.
- **Resolution clamp:** each map's longest edge ≤ **1024 px** (box-downscale larger source art on load).
- **Memory ceiling:** total material VRAM ≤ **768 MB**, counting the full mip chain (~1.33× base). On exceeding it, drop the **lowest-traffic** remaining materials back to paletted (`usePBR = 0`). *Traffic* = total world surface-area of that material's texnum in the current map. Upload in **descending-traffic order**; hero materials pinned above derived regardless of area. **No silent truncation** — log every material's resolution, running MB, and everything skipped/downscaled/dropped.
- **POM (option a, primary-hit only):** `flags:pom` **and** `maps[6] != -1`; `kPomHeightScale = 0.06` of a UV tile; **16 steps at normal incidence ramping to 32 at grazing** (linear in `1 − N·V`) + one bisection step; **white = raised**; height taps at **LOD 0**; bound the parallax **offset** magnitude (not the coordinate) and sample through the **REPEAT** sampler — never absolute-clamp UV to `[0,1]`. `uv_scale` multiplies raw UVs **first**, then POM marches in that scaled space. `noPom` skips the march.
- **Normal mapping:** OpenGL **Y+** (green channel un-flipped); tangent **and** bitangent from `∂P/∂U` and `∂P/∂V` (triangle edges + UV deltas), Gram-Schmidt-orthogonalized against the geometric normal; degenerate-UV (`det ≈ 0`) → arbitrary orthonormal frame.
- **AO:** multiply into the **indirect/ambient** term only (GI bounce + sky/sector ambient) — never the direct flashlight/NEE contribution.
- **Emissive:** `emissive.rgb × kEmissiveScale` added on the **primary hit only**; **not** registered as an NEE emitter in v1. Start `kEmissiveScale = 40.0` (from `emissive_derive.h:52`); re-tune expected (lower).
- **Failure = never crash:** missing/undecodable PNG, absent file named by a row, malformed row (wrong column count / unknown `source` / unknown `flags` token) → that material falls back to paletted + one-line warning; the map still loads. No `materials.csv` ⇒ every material paletted (Ultra shows Solid's art). Duplicate `doom_name` ⇒ last row wins, logged.
- **Sprites paletted in v1.** v1 HD covers **walls + flats only**. `flags:sprite` + the generator's alpha-carry are plumbing for a fast-follow; no `sprite` rows ship in v1's CSV.
- **Asset location:** `DOOMASSETDIR` env var, default `assets/ultra/` relative to the executable/repo root (mirrors `DOOMWADDIR`, `d_main.c:740-742`).
- **Vendored dep:** `stb_image.h` (PD/MIT, no new *link* dep). Record its version in `docs/standards/dependencies.md` §"Where this project's dependencies live".
- **Build (always):** after any engine change run `make` **and** `make test` from `linuxdoom-1.10/`; both clean, no new warnings; shaders compile with `glslc` at 0 warnings. Never leave building to the user.
- **Commit trailer (every commit):**
  ```
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
  ```
  Commit subjects use `DOOM-0042: <imperative description>` (one concern per commit).

---

## File Structure

New files (created by this plan):

- `assets/ultra/materials.csv` — the hand-maintained name→PBR-set sidecar (repo-tracked).
- `assets/ultra/LICENSES` — hero-material provenance (repo-tracked).
- `assets/ultra/.gitignore` — ignores `derived/` (WAD-derivative output stays out of the repo).
- `assets/ultra/heroes/**` — committed CC0 hero PNG map sets (tiny).
- `assets/ultra/derived/**` — generated PBR maps (gitignored; regenerated by the script).
- `scripts/pbr_derive.py` — offline generator (standalone; **not** in the engine build).
- `linuxdoom-1.10/stb_image.h` — vendored public-domain PNG decoder.
- `linuxdoom-1.10/rb_image.h` / `rb_image.c` — thin decode+downscale wrapper around stb. `rb_image.c` is the **single** TU that defines `STB_IMAGE_IMPLEMENTATION`; it goes in `OBJS`.
- `linuxdoom-1.10/rb_materials.h` — **header-only** (`inline` functions in an include guard, the `emissive_derive.h` convention): sidecar parse, name→id resolution, per-id control table, traffic/budget decisions (pure CPU; unit-tested). **No `.c`, not in `OBJS`** — included by `r_vulkan.cpp` and the test directly.
- `linuxdoom-1.10/tests/rb_materials_test.cpp`, `linuxdoom-1.10/tests/rb_image_test.cpp` — unit tests (auto-discovered by `make test`).

**Test-linkage convention (load-bearing):** `make test` compiles each `tests/*_test.cpp` as **one translation unit** (`$(CXX) $< -o $@` — no separate objects linked). So all tested logic must be reachable from a single `#include`: `rb_materials.h` is header-only (test includes the header); `rb_image_test.cpp` `#include "../rb_image.c"` to pull the stb implementation + wrappers into its own TU. This matches `tests/emissive_derive_test.cpp` (header-only `emissive_derive.h`).

Modified files:

- `linuxdoom-1.10/Makefile` — add `rb_image.o` to `OBJS` (rb_materials is header-only, no object).
- `linuxdoom-1.10/r_vulkan.cpp` — new linear+mip sampler; HD RGBA8 bindless array + mip-gen; new HD descriptor set; control SSBO; `EnsureHdMaterials()`; tier hook in `RB_Vulkan_BuildLevel`.
- `linuxdoom-1.10/shaders/pathtrace.comp` — bind the HD set; `usePBR` branch (albedo/normal/AO/emissive/POM).
- `docs/standards/dependencies.md` — vendored `stb_image.h` version record.

**Why these boundaries:** all decision logic (which material is HD, at what resolution, dropped or not) lives in `rb_materials.h` as header-only pure functions with no Vulkan/GPU dependency, so `make test` covers it on CPU. `r_vulkan.cpp` only *consumes* the resolved table to allocate/upload GPU resources. `rb_image.c` isolates the one external decoder behind a 3-function interface.

**Execution checkpoints:** Tasks 1–5 (Phase A/B-CPU) are offline/CPU and can proceed with no GPU. **The first on-hardware visible result is Task 10** (hero albedo in Ultra on E1M1). Tasks 16–17 (derive generator, curated heroes) complete map coverage after the path is proven.

---

## Task 1: Asset scaffold — sidecar, gitignore, licence stub, bring-up hero

**Files:**
- Create: `assets/ultra/materials.csv`
- Create: `assets/ultra/.gitignore`
- Create: `assets/ultra/LICENSES`
- Create: `assets/ultra/heroes/bringup/` PNGs (synthetic placeholder set — replaced by curated CC0 in Task 17)
- Create: `scripts/make_bringup_hero.py` (one-off placeholder generator)

**Interfaces:**
- Produces: the on-disk asset root that `DOOMASSETDIR` resolves to; the CSV schema every later task parses; one hero row (`STARTAN3`) resolvable on E1M1 so engine tasks have real PNGs to load before curated art exists.

- [ ] **Step 1: Write the sidecar with the schema header + one hero row + a few derive rows**

`assets/ultra/materials.csv`:
```
#doom_name,source,albedo,normal,roughness,metallic,ao,emissive,height,uv_scale,flags
# Hero rows name their PNG map files (relative to assets/ultra/). Derive rows leave
# map columns blank — the loader resolves derived/<doom_name>_<suffix>.png by convention.
# v1 = walls + flats only; no sprite rows. flags is |-separated (pom|noPom|sprite).
STARTAN3,hero,heroes/bringup/startan3_alb.png,heroes/bringup/startan3_nrm.png,,,heroes/bringup/startan3_ao.png,,heroes/bringup/startan3_hgt.png,1.0,pom
TEKWALL1,derive,,,,,,,,1.0,pom
FLOOR4_8,derive,,,,,,,,1.0,noPom
```
(Roughness/metallic columns are intentionally blank in v1 even for heroes — DOOM-0103 territory.)

- [ ] **Step 2: Gitignore the derived output**

`assets/ultra/.gitignore`:
```
# Generated PBR maps are derivative works of the WAD art — out of the repo, like the WAD.
derived/
```

- [ ] **Step 3: Start the licence ledger**

`assets/ultra/LICENSES`:
```
# DOOM-0042 hero-material provenance. Every committed hero PNG traces to a CC0/free source here.
# Format: <path>  <source-url-or-library>  <licence>
heroes/bringup/*  synthetic placeholder (scripts/make_bringup_hero.py)  CC0 (generated, no third-party art)
```

- [ ] **Step 4: Generate a synthetic bring-up hero set (unblocks engine tasks without curated art)**

`scripts/make_bringup_hero.py` — writes four small PNGs (albedo, normal, ao, height) for `STARTAN3` using only the Python stdlib + a minimal PNG writer, so no external asset or pip install is needed. Normal is flat (`128,128,255` = +Z), height a diagonal ramp, ao mid-grey, albedo a blue-grey tech tint:
```python
#!/usr/bin/env python3
"""One-off: synthesize a placeholder hero map set so the engine HD path can be
brought up before curated CC0 art is staged (Task 17 replaces these)."""
import struct, zlib, os, math

def write_png(path, w, h, rgb_fn):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (none) per scanline
        for x in range(w):
            r, g, b = rgb_fn(x, y, w, h)
            raw += bytes((r & 255, g & 255, b & 255))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))  # 8-bit RGB
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(png)

D = "assets/ultra/heroes/bringup"
write_png(f"{D}/startan3_alb.png", 64, 128, lambda x, y, w, h: (70, 90, 110) if (x // 8 + y // 8) % 2 else (60, 78, 96))
write_png(f"{D}/startan3_nrm.png", 64, 128, lambda x, y, w, h: (128, 128, 255))
write_png(f"{D}/startan3_ao.png",  64, 128, lambda x, y, w, h: (180, 180, 180))
write_png(f"{D}/startan3_hgt.png", 64, 128, lambda x, y, w, h: (int(255 * (x / w)),) * 3)
print("wrote bring-up hero set to", D)
```

- [ ] **Step 5: Run it and verify the PNGs exist**

Run: `cd /mnt/Games/Scripts/Linux/DOOM_Ants && python3 scripts/make_bringup_hero.py && ls -1 assets/ultra/heroes/bringup/`
Expected: prints the four `startan3_*.png` files.

- [ ] **Step 6: Commit**

```bash
cd /mnt/Games/Scripts/Linux/DOOM_Ants
git add assets/ultra/materials.csv assets/ultra/.gitignore assets/ultra/LICENSES \
        assets/ultra/heroes/bringup scripts/make_bringup_hero.py
git commit -m "DOOM-0042: add Ultra material sidecar scaffold + bring-up hero set

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Sidecar parser — rows + flags (header-only, unit-tested)

**Files:**
- Create: `linuxdoom-1.10/rb_materials.h` (header-only — `inline` functions in an include guard, the `emissive_derive.h` convention; **no `.c`**)
- Test: `linuxdoom-1.10/tests/rb_materials_test.cpp`

**Interfaces:**
- Produces:
  - `#define RB_MAP_COUNT 7` and map-index enum `RB_ALB=0,RB_NRM,RB_RGH,RB_MET,RB_AO,RB_EMIS,RB_HGT`.
  - `#define RB_FLAG_POM 1u`, `RB_FLAG_NOPOM 2u`, `RB_FLAG_SPRITE 4u`.
  - `typedef struct { char name[9]; int is_hero; char maps[RB_MAP_COUNT][128]; float uv_scale; unsigned int flags; } rb_matrow_t;`
  - `int rb_parse_material_line(const char* line, rb_matrow_t* out);` → `1` = a data row parsed into `*out`; `0` = blank/comment (skip); `-1` = malformed (caller logs + skips). Trims whitespace; keys columns by position; `#` line or empty → 0.
  - `unsigned int rb_parse_flags(const char* field);` → bitmask; `noPom` clears `pom` if both present; unknown token → sets a sentinel bit `RB_FLAG_BAD` (`0x80000000u`) so the caller can treat the row malformed.

- [ ] **Step 1: Write the failing test**

`linuxdoom-1.10/tests/rb_materials_test.cpp`:
```cpp
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../rb_materials.h"   // header-only inline funcs; C++ TU, no extern "C"

static void test_hero_row() {
    rb_matrow_t r;
    int rc = rb_parse_material_line(
        "STARTAN3,hero,heroes/m/a.png,heroes/m/n.png,,,heroes/m/o.png,,heroes/m/h.png,2.0,pom", &r);
    assert(rc == 1);
    assert(strcmp(r.name, "STARTAN3") == 0);
    assert(r.is_hero == 1);
    assert(strcmp(r.maps[RB_ALB], "heroes/m/a.png") == 0);
    assert(strcmp(r.maps[RB_NRM], "heroes/m/n.png") == 0);
    assert(r.maps[RB_RGH][0] == '\0');           // blank cell = no map
    assert(strcmp(r.maps[RB_AO], "heroes/m/o.png") == 0);
    assert(strcmp(r.maps[RB_HGT], "heroes/m/h.png") == 0);
    assert(r.uv_scale == 2.0f);
    assert(r.flags == RB_FLAG_POM);
}

static void test_derive_row_blank_maps() {
    rb_matrow_t r;
    int rc = rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &r);
    assert(rc == 1);
    assert(r.is_hero == 0);
    for (int i = 0; i < RB_MAP_COUNT; i++) assert(r.maps[i][0] == '\0');
    assert(r.uv_scale == 1.0f);
    assert(r.flags == RB_FLAG_NOPOM);            // noPom set, pom not
}

static void test_comment_and_blank() {
    rb_matrow_t r;
    assert(rb_parse_material_line("#doom_name,source,...", &r) == 0);
    assert(rb_parse_material_line("   ", &r) == 0);
}

static void test_flags_pom_and_nopom_clears_pom() {
    assert(rb_parse_flags("pom") == RB_FLAG_POM);
    assert(rb_parse_flags("pom|noPom") == RB_FLAG_NOPOM);   // both -> noPom wins, pom cleared
    assert((rb_parse_flags("bogus") & RB_FLAG_BAD) != 0);
}

static void test_malformed_wrong_column_count() {
    rb_matrow_t r;
    assert(rb_parse_material_line("ONLY,three,cols", &r) == -1);
    assert(rb_parse_material_line("NAME,banana,,,,,,,,1.0,pom", &r) == -1); // unknown source
}

int main() {
    test_hero_row();
    test_derive_row_blank_maps();
    test_comment_and_blank();
    test_flags_pom_and_nopom_clears_pom();
    test_malformed_wrong_column_count();
    printf("rb_materials parse: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test`
Expected: FAIL — `rb_materials.h: No such file or directory`.

- [ ] **Step 3: Write the header-only module (types + inline implementations)**

`linuxdoom-1.10/rb_materials.h` — the whole module is `inline` functions in an include guard (the `emissive_derive.h` convention), so both `r_vulkan.cpp` and the single-TU test reach it by `#include` alone. All helpers are prefixed `rb_` to avoid polluting the including TU. No `strtok_r` (not reliably present on the mingw Windows cross-build) — hand-split on `|` like the column split:
```c
#ifndef RB_MATERIALS_H
#define RB_MATERIALS_H
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define RB_MAP_COUNT 7
enum { RB_ALB = 0, RB_NRM, RB_RGH, RB_MET, RB_AO, RB_EMIS, RB_HGT };

#define RB_FLAG_POM    1u
#define RB_FLAG_NOPOM  2u
#define RB_FLAG_SPRITE 4u
#define RB_FLAG_BAD    0x80000000u   /* unknown flags token — caller treats row malformed */

typedef struct {
    char          name[9];                     /* DOOM name, NUL-terminated (<=8 chars) */
    int           is_hero;                     /* 1 = hero, 0 = derive */
    char          maps[RB_MAP_COUNT][128];     /* per-map path; "" = no map */
    float         uv_scale;
    unsigned int  flags;
} rb_matrow_t;

/* Trim leading/trailing ASCII whitespace in place; returns the trimmed start. */
static inline char* rb_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static inline unsigned int rb_parse_flags(const char* field) {
    unsigned int f = 0;
    char buf[128];
    strncpy(buf, field, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* start = buf;
    for (char* c = buf; ; c++) {
        if (*c == '|' || *c == '\0') {
            char end = *c; *c = '\0';
            char* tok = rb_trim(start);
            if (*tok) {
                if      (!strcmp(tok, "pom"))    f |= RB_FLAG_POM;
                else if (!strcmp(tok, "noPom"))  f |= RB_FLAG_NOPOM;
                else if (!strcmp(tok, "sprite")) f |= RB_FLAG_SPRITE;
                else                             f |= RB_FLAG_BAD;
            }
            if (end == '\0') break;
            start = c + 1;
        }
    }
    if (f & RB_FLAG_NOPOM) f &= ~RB_FLAG_POM;   /* noPom wins; shader checks one bit */
    return f;
}

/* 1 = data row parsed, 0 = comment/blank (skip), -1 = malformed (log + skip). */
static inline int rb_parse_material_line(const char* line, rb_matrow_t* out) {
    char buf[1024];
    strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* s = rb_trim(buf);
    if (!*s || *s == '#') return 0;             /* comment / blank */

    /* Hand-split into exactly 11 positional columns on ',' (strtok would collapse
       empty cells, which a derive row needs to keep). */
    char* col[11];
    int n = 0;
    col[n++] = s;
    for (char* c = s; *c; c++)
        if (*c == ',') { *c = '\0'; if (n < 11) col[n] = c + 1; n++; }
    if (n != 11) return -1;                     /* wrong column count */

    memset(out, 0, sizeof(*out));
    strncpy(out->name, rb_trim(col[0]), 8); out->name[8] = '\0';

    char* src = rb_trim(col[1]);
    if      (!strcmp(src, "hero"))   out->is_hero = 1;
    else if (!strcmp(src, "derive")) out->is_hero = 0;
    else return -1;                             /* unknown source */

    for (int i = 0; i < RB_MAP_COUNT; i++)
        strncpy(out->maps[i], rb_trim(col[2 + i]), sizeof(out->maps[i]) - 1);

    out->uv_scale = (float)atof(rb_trim(col[9]));
    if (out->uv_scale <= 0.0f) out->uv_scale = 1.0f;   /* blank/invalid -> 1.0 */

    out->flags = rb_parse_flags(rb_trim(col[10]));
    if (out->flags & RB_FLAG_BAD) return -1;    /* unknown flags token -> malformed */
    return 1;
}

#endif
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test && ./linux/rb_materials_test`
Expected: PASS — `rb_materials parse: all passed`.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/rb_materials.h linuxdoom-1.10/tests/rb_materials_test.cpp
git commit -m "DOOM-0042: sidecar row + flags parser (rb_materials, header-only)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Name → unified material id resolution + per-id control table

**Files:**
- Modify: `linuxdoom-1.10/rb_materials.h` (append inline funcs — header-only, no `.c`)
- Test: `linuxdoom-1.10/tests/rb_materials_test.cpp` (append cases)

**Interfaces:**
- Consumes: `rb_matrow_t` from Task 2.
- Produces:
  - `typedef struct { int maps[RB_MAP_COUNT]; float uvScale; unsigned int flags; unsigned int usePBR; } rb_matctrl_t;` — the GPU control struct (std430-compatible; validated `sizeof == 40` in the test).
  - `typedef int (*rb_name_resolver_t)(const char* name, int* out_id);` — returns `1` + writes the unified id if `name` resolves (wall texnum, or `numWall + flatIdx`), else `0`. Injected so the resolution logic is testable without the WAD (the engine passes a real resolver in Task 9).
  - `void rb_build_ctrl_table(const rb_matrow_t* rows, int nrows, int nmaterials, rb_name_resolver_t resolve, rb_matctrl_t* table, int* dup_count);` — zero-inits `table[0..nmaterials)`, then for each row resolves the id and fills that slot (`usePBR=1`, `maps[]=-1` placeholders until image load assigns slots, `uvScale`, `flags`). A row whose name doesn't resolve is skipped. A second row hitting an already-filled id overwrites it (last-wins) and increments `*dup_count`.

- [ ] **Step 1: Write the failing test (append to rb_materials_test.cpp)**

```cpp
// --- Task 3 additions ---
#include <map>
#include <string>
static std::map<std::string,int>* g_names;      // test fixture: name -> id
static int test_resolver(const char* name, int* out_id) {
    auto it = g_names->find(name);
    if (it == g_names->end()) return 0;
    *out_id = it->second; return 1;
}
static void test_ctrl_table_build() {
    static_assert(sizeof(rb_matctrl_t) == 40, "std430 control struct must be 40 bytes");
    std::map<std::string,int> names = { {"STARTAN3", 5}, {"FLOOR4_8", 12} };
    g_names = &names;
    rb_matrow_t rows[2];
    assert(rb_parse_material_line("STARTAN3,hero,a.png,,,,,,,1.5,pom", &rows[0]) == 1);
    assert(rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &rows[1]) == 1);
    rb_matctrl_t table[16];
    int dups = -1;
    rb_build_ctrl_table(rows, 2, 16, test_resolver, table, &dups);
    assert(dups == 0);
    assert(table[5].usePBR == 1 && table[5].flags == RB_FLAG_POM && table[5].uvScale == 1.5f);
    assert(table[12].usePBR == 1 && table[12].flags == RB_FLAG_NOPOM);
    assert(table[0].usePBR == 0);                // untouched material stays paletted
    for (int i = 0; i < RB_MAP_COUNT; i++) assert(table[5].maps[i] == -1); // slots unassigned yet
}
static void test_ctrl_table_dup_last_wins() {
    std::map<std::string,int> names = { {"TEKWALL1", 3} };
    g_names = &names;
    rb_matrow_t rows[2];
    rb_parse_material_line("TEKWALL1,hero,a.png,,,,,,,1.0,pom",   &rows[0]);
    rb_parse_material_line("TEKWALL1,derive,,,,,,,,4.0,noPom",     &rows[1]);
    rb_matctrl_t table[8]; int dups = 0;
    rb_build_ctrl_table(rows, 2, 8, test_resolver, table, &dups);
    assert(dups == 1);
    assert(table[3].uvScale == 4.0f && table[3].flags == RB_FLAG_NOPOM); // last row won
}
// add to main():
//   test_ctrl_table_build(); test_ctrl_table_dup_last_wins();
```
Add the two calls to `main()` before the final `printf`.

- [ ] **Step 2: Run to verify it fails**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test`
Expected: FAIL — `rb_build_ctrl_table` / `rb_matctrl_t` undeclared.

- [ ] **Step 3: Append the types + inline implementation to the header (before the final `#endif`)**

```c
/* GPU control struct — MUST match the std430 MatCtrl struct in pathtrace.comp.
   int maps[7]=28, float uvScale=32, uint flags=36, uint usePBR=40 (no padding). */
typedef struct {
    int           maps[RB_MAP_COUNT];
    float         uvScale;
    unsigned int  flags;
    unsigned int  usePBR;
} rb_matctrl_t;

typedef int (*rb_name_resolver_t)(const char* name, int* out_id);

static inline void rb_build_ctrl_table(const rb_matrow_t* rows, int nrows, int nmaterials,
                         rb_name_resolver_t resolve, rb_matctrl_t* table, int* dup_count) {
    for (int i = 0; i < nmaterials; i++) {
        for (int m = 0; m < RB_MAP_COUNT; m++) table[i].maps[m] = -1;
        table[i].uvScale = 1.0f;
        table[i].flags   = 0u;
        table[i].usePBR  = 0u;                  /* default: paletted */
    }
    int dups = 0;
    for (int r = 0; r < nrows; r++) {
        int id = -1;
        if (!resolve(rows[r].name, &id)) continue;   /* name not in this WAD */
        if (id < 0 || id >= nmaterials)  continue;
        if (table[id].usePBR) dups++;                /* already set -> last-wins */
        for (int m = 0; m < RB_MAP_COUNT; m++) table[id].maps[m] = -1;  /* image load fills these */
        table[id].uvScale = rows[r].uv_scale;
        table[id].flags   = rows[r].flags;
        table[id].usePBR  = 1u;
    }
    if (dup_count) *dup_count = dups;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test && ./linux/rb_materials_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/rb_materials.h linuxdoom-1.10/tests/rb_materials_test.cpp
git commit -m "DOOM-0042: name->id resolution + per-id control table

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: PNG decode + box-downscale (vendored stb_image, unit-tested)

**Files:**
- Create: `linuxdoom-1.10/stb_image.h` (vendored, unmodified upstream)
- Create: `linuxdoom-1.10/rb_image.h`
- Create: `linuxdoom-1.10/rb_image.c`
- Test: `linuxdoom-1.10/tests/rb_image_test.cpp`
- Modify: `docs/standards/dependencies.md`

**Interfaces:**
- Produces:
  - `typedef struct { unsigned char* pixels; int w, h; } rb_image_t;` — always 4-channel RGBA8, tightly packed, `pixels` owned (free with `rb_image_free`).
  - `int rb_image_load(const char* path, rb_image_t* out);` → `1` on success (decoded to RGBA8), `0` on any failure (missing/undecodable) — never aborts.
  - `void rb_image_downscale_max(rb_image_t* img, int max_edge);` — if the longest edge exceeds `max_edge`, box-filter-downscale in place to fit (integer or fractional box, RGBA averaged). No-op if already within bounds.
  - `void rb_image_free(rb_image_t* img);`

- [ ] **Step 1: Vendor stb_image.h**

Run: `cd linuxdoom-1.10 && curl -fsSL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o stb_image.h && head -3 stb_image.h`
Expected: prints the stb header banner. Note the version string near the top (`stb_image - v2.xx`) for Step 7.
*(If offline: copy the file from any local stb checkout. It is a single self-contained header.)*

- [ ] **Step 2: Write the failing test**

`linuxdoom-1.10/tests/rb_image_test.cpp`:
```cpp
#include <cassert>
#include <cstdio>
#include "../rb_image.c"   // single-TU: pulls in the stb impl + wrappers.
                           // (rb_image.h self-guards its decls with extern "C".)

// Fixtures are the committed bring-up PNGs from Task 1 — no fragile on-the-fly
// generation. startan3_ao.png is a solid 180-grey 64x128 field, so a downscale must
// preserve ~180 everywhere: a clean box-average check. Paths are relative to
// linuxdoom-1.10/ (the CWD `make test` runs the binary from).
static const char* AO = "../assets/ultra/heroes/bringup/startan3_ao.png";

int main() {
    rb_image_t img;
    assert(rb_image_load("/does/not/exist.png", &img) == 0);   // failure never crashes

    assert(rb_image_load(AO, &img) == 1);
    assert(img.w == 64 && img.h == 128);
    assert(img.pixels[0] >= 176 && img.pixels[0] <= 184);      // ~180 grey
    assert(img.pixels[3] == 255);                              // RGB source -> opaque alpha

    rb_image_downscale_max(&img, 32);                          // longest edge 128 -> 32 (=> 16x32)
    assert(img.w == 16 && img.h == 32);
    for (int i = 0; i < img.w * img.h; i++) {                  // solid field stays ~180
        assert(img.pixels[i * 4 + 0] >= 176 && img.pixels[i * 4 + 0] <= 184);
        assert(img.pixels[i * 4 + 3] == 255);
    }
    rb_image_free(&img);
    printf("rb_image: all passed\n");
    return 0;
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd linuxdoom-1.10 && make linux/rb_image_test`
Expected: FAIL — `../rb_image.c: No such file or directory` (the test includes it; it doesn't exist yet).

- [ ] **Step 4: Write the wrapper header + impl**

`linuxdoom-1.10/rb_image.h` — the `extern "C"` guard is load-bearing: `rb_image.c` is compiled as C (gcc → C-linkage symbols in `rb_image.o`), but the engine TU `r_vulkan.cpp` is C++; without the guard the C++ side would mangle the names and fail to link.
```c
#ifndef RB_IMAGE_H
#define RB_IMAGE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { unsigned char* pixels; int w, h; } rb_image_t;   /* always RGBA8 */
int  rb_image_load(const char* path, rb_image_t* out);            /* 1 ok, 0 fail (no crash) */
void rb_image_downscale_max(rb_image_t* img, int max_edge);       /* box-filter in place */
void rb_image_free(rb_image_t* img);
#ifdef __cplusplus
}
#endif
#endif
```

`linuxdoom-1.10/rb_image.c`:
```c
#include "rb_image.h"
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG            /* v1 ships PNG heroes/derived only */
/* Do NOT define STBI_NO_STDIO: stb guards stbi_load(path,...) with a bare
   `#ifndef STBI_NO_STDIO`, so even `#define STBI_NO_STDIO 0` strips the loader.
   stdio support is stb's default — leave it undefined. */
/* STBI_ONLY_PNG leaves two int-overflow helpers (stbi__mul2shorts_valid /
   stbi__addints_valid) compiled-but-unused, so vendored stb_image.h trips
   -Wunused-function under -Wall. We don't edit vendored code (dependency rule),
   and can't drop STBI_ONLY_PNG without pulling in every decoder — so scope-silence
   just this header's warnings, not the project's. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop

int rb_image_load(const char* path, rb_image_t* out) {
    int w = 0, h = 0, comp = 0;
    unsigned char* p = stbi_load(path, &w, &h, &comp, 4);   /* force RGBA */
    if (!p) return 0;
    out->pixels = p; out->w = w; out->h = h;
    return 1;
}

/* Box-filter downscale so the longest edge is <= max_edge. Averages the source
   texels covered by each destination texel (fractional box, area-weighted-ish via
   nearest integer coverage — good enough for material maps at <=1024). */
void rb_image_downscale_max(rb_image_t* img, int max_edge) {
    int longest = img->w > img->h ? img->w : img->h;
    if (longest <= max_edge || longest == 0) return;
    double s = (double)max_edge / (double)longest;
    int nw = (int)(img->w * s); if (nw < 1) nw = 1;
    int nh = (int)(img->h * s); if (nh < 1) nh = 1;
    unsigned char* dst = (unsigned char*)malloc((size_t)nw * nh * 4);
    if (!dst) return;                       /* OOM: leave img unchanged (never crash) */
    for (int y = 0; y < nh; y++) {
        int sy0 = (int)((double)y     * img->h / nh);
        int sy1 = (int)((double)(y+1) * img->h / nh); if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < nw; x++) {
            int sx0 = (int)((double)x     * img->w / nw);
            int sx1 = (int)((double)(x+1) * img->w / nw); if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned acc[4] = {0,0,0,0}, cnt = 0;
            for (int sy = sy0; sy < sy1 && sy < img->h; sy++)
                for (int sx = sx0; sx < sx1 && sx < img->w; sx++) {
                    const unsigned char* sp = img->pixels + ((size_t)sy * img->w + sx) * 4;
                    for (int c = 0; c < 4; c++) acc[c] += sp[c];
                    cnt++;
                }
            unsigned char* dp = dst + ((size_t)y * nw + x) * 4;
            for (int c = 0; c < 4; c++) dp[c] = (unsigned char)(cnt ? acc[c] / cnt : 0);
        }
    }
    free(img->pixels);
    img->pixels = dst; img->w = nw; img->h = nh;
}

void rb_image_free(rb_image_t* img) {
    if (img && img->pixels) { stbi_image_free(img->pixels); img->pixels = NULL; }
}
```
*Note:* `rb_image_downscale_max` reallocates with `malloc` but the original `stbi_load` buffer is freed with `stbi_image_free`. That is safe here (stb's default allocator is `malloc`/`free`), but to be unambiguous the downscale path frees the old buffer with `free` (matching its `malloc`) — after a downscale, `rb_image_free`'s `stbi_image_free` still calls `free` on a `malloc`'d buffer, which is correct. Leave a one-line comment saying so.

- [ ] **Step 5: Run to verify it passes**

Run: `cd linuxdoom-1.10 && make linux/rb_image_test && ./linux/rb_image_test`
Expected: PASS — `rb_image: all passed`.

- [ ] **Step 6: Record the vendored dependency**

Add to `docs/standards/dependencies.md` §"Where this project's dependencies live" a vendored-single-header entry:
```
- `linuxdoom-1.10/stb_image.h` — vendored public-domain PNG decoder (stb_image v2.xx),
  added for DOOM-0042 HD material loading. No link dependency. Re-check upstream on a
  sweep cadence; update the version noted here when bumped.
```
(Use the actual version from Step 1.)

- [ ] **Step 7: Commit**

```bash
git add linuxdoom-1.10/stb_image.h linuxdoom-1.10/rb_image.h linuxdoom-1.10/rb_image.c \
        linuxdoom-1.10/tests/rb_image_test.cpp docs/standards/dependencies.md
git commit -m "DOOM-0042: vendored stb_image PNG decode + box downscale (rb_image)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Traffic-ordered budget — drop lowest-traffic materials to paletted

**Files:**
- Modify: `linuxdoom-1.10/rb_materials.h` (append inline funcs — header-only, no `.c`)
- Test: `linuxdoom-1.10/tests/rb_materials_test.cpp` (append)

**Interfaces:**
- Consumes: the `rb_matctrl_t` table (Task 3), per-id traffic areas, and per-id estimated MB.
- Produces:
  - `void rb_apply_budget(rb_matctrl_t* table, int nmaterials, const float* traffic, const float* est_mb, const int* is_hero, float ceiling_mb, int* order_out, int* n_loaded);` — computes an upload order (descending traffic, heroes pinned first), accumulates `est_mb` in that order, and for every id whose cumulative total would exceed `ceiling_mb` sets `usePBR = 0` (dropped to paletted). `order_out` receives the ids to upload (loaded ones only), `*n_loaded` their count. Materials with `usePBR == 0` going in (no CSV row) are ignored entirely.

- [ ] **Step 1: Write the failing test (append)**

```cpp
static void test_budget_drops_lowest_traffic() {
    // 3 HD materials, each ~2 MB, ceiling 5 MB -> two fit, the third (lowest traffic) drops.
    rb_matctrl_t table[3];
    for (int i = 0; i < 3; i++) { table[i].usePBR = 1; table[i].flags = 0; table[i].uvScale = 1; }
    float traffic[3] = { 100.0f, 300.0f, 50.0f };   // id1 biggest, id2 smallest
    float est_mb[3]  = { 2.0f, 2.0f, 2.0f };
    int   is_hero[3] = { 0, 0, 0 };
    int order[3], nloaded = -1;
    rb_apply_budget(table, 3, traffic, est_mb, is_hero, 5.0f, order, &nloaded);
    assert(nloaded == 2);
    assert(order[0] == 1 && order[1] == 0);         // descending traffic
    assert(table[1].usePBR == 1 && table[0].usePBR == 1);
    assert(table[2].usePBR == 0);                   // lowest-traffic dropped
}
static void test_budget_pins_hero_over_bigger_derived() {
    rb_matctrl_t table[2];
    for (int i = 0; i < 2; i++) { table[i].usePBR = 1; table[i].uvScale = 1; table[i].flags = 0; }
    float traffic[2] = { 10.0f, 999.0f };           // id1 huge traffic but derived
    float est_mb[2]  = { 4.0f, 4.0f };
    int   is_hero[2] = { 1, 0 };                     // id0 is a hero
    int order[2], nloaded = 0;
    rb_apply_budget(table, 2, traffic, est_mb, is_hero, 5.0f, order, &nloaded);
    assert(nloaded == 1 && order[0] == 0);           // hero pinned first, derived dropped
    assert(table[0].usePBR == 1 && table[1].usePBR == 0);
}
// add to main(): test_budget_drops_lowest_traffic(); test_budget_pins_hero_over_bigger_derived();
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test`
Expected: FAIL — `rb_apply_budget` undeclared.

- [ ] **Step 3: Append the inline implementation to the header (before the final `#endif`)**

`<stdlib.h>` (qsort/malloc) is already included at the top of the header from Task 2, so don't re-include it.
```c
typedef struct { int id; float traffic; int is_hero; } rb_sortent_t;

static inline int rb_cmp_ent(const void* a, const void* b) {
    const rb_sortent_t* x = (const rb_sortent_t*)a;
    const rb_sortent_t* y = (const rb_sortent_t*)b;
    if (x->is_hero != y->is_hero) return y->is_hero - x->is_hero;   /* heroes first */
    if (x->traffic < y->traffic) return 1;                          /* then desc traffic */
    if (x->traffic > y->traffic) return -1;
    return x->id - y->id;                                           /* stable tiebreak */
}

static inline void rb_apply_budget(rb_matctrl_t* table, int nmaterials,
                     const float* traffic, const float* est_mb, const int* is_hero,
                     float ceiling_mb, int* order_out, int* n_loaded) {
    rb_sortent_t* ent = (rb_sortent_t*)malloc((size_t)nmaterials * sizeof(rb_sortent_t));
    int m = 0;
    for (int i = 0; i < nmaterials; i++)
        if (table[i].usePBR) { ent[m].id = i; ent[m].traffic = traffic[i]; ent[m].is_hero = is_hero[i]; m++; }
    qsort(ent, m, sizeof(rb_sortent_t), rb_cmp_ent);

    float used = 0.0f;
    int n = 0;
    for (int k = 0; k < m; k++) {
        int id = ent[k].id;
        if (used + est_mb[id] <= ceiling_mb) {
            used += est_mb[id];
            order_out[n++] = id;                /* loaded, in upload order */
        } else {
            table[id].usePBR = 0;               /* over budget -> paletted */
        }
    }
    *n_loaded = n;
    free(ent);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd linuxdoom-1.10 && make linux/rb_materials_test && ./linux/rb_materials_test`
Expected: PASS.

- [ ] **Step 5: Full test sweep + commit**

Run: `cd linuxdoom-1.10 && make test`
Expected: all existing tests + the two new ones PASS.
```bash
git add linuxdoom-1.10/rb_materials.h linuxdoom-1.10/tests/rb_materials_test.cpp
git commit -m "DOOM-0042: traffic-ordered VRAM budget (drop lowest-traffic to paletted)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Wire rb_image + rb_materials into the build; add the linear+mip sampler

**Files:**
- Modify: `linuxdoom-1.10/Makefile:82-147` (OBJS list)
- Modify: `linuxdoom-1.10/r_vulkan.cpp` (VulkanState: new sampler handle + creation near `:3644`)

**Interfaces:**
- Consumes: nothing new.
- Produces: `g.hdSampler` — a `VkSampler` with `VK_FILTER_LINEAR` min/mag, `VK_SAMPLER_MIPMAP_MODE_LINEAR`, `VK_SAMPLER_ADDRESS_MODE_REPEAT` on U/V/W, `maxLod = VK_LOD_CLAMP_NONE`; the HD-array counterpart to the nearest paletted `g.texSampler`. Also `rb_image.o` linked into the engine (`rb_materials` is header-only — no object).

- [ ] **Step 1: Add the rb_image object to OBJS**

In `linuxdoom-1.10/Makefile`, in the `OBJS=` list (after `$(O)/r_mesh.o`), add:
```make
		$(O)/rb_image.o		\
```
(No `rb_materials.o` — that module is header-only, compiled inline into `r_vulkan.o` where it is `#include`d.)

- [ ] **Step 2: Declare the sampler handle**

In `r_vulkan.cpp`, in `struct VulkanState` near the existing `VkSampler texSampler;` / `VkSampler compositeSampler;` declarations, add:
```cpp
    VkSampler hdSampler = VK_NULL_HANDLE;   // DOOM-0042: linear+mip+REPEAT for HD PBR maps
```

- [ ] **Step 3: Create it beside the paletted sampler**

In `r_vulkan.cpp`, immediately after the `g.texSampler` creation block (ends `r_vulkan.cpp:3644`), add:
```cpp
    // DOOM-0042: HD material sampler — linear filtering + full mip chain + REPEAT
    // tiling (walls tile U 0..N). Distinct from the nearest paletted g.texSampler.
    VkSamplerCreateInfo hsci = {};
    hsci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    hsci.magFilter = VK_FILTER_LINEAR;
    hsci.minFilter = VK_FILTER_LINEAR;
    hsci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    hsci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.maxLod = VK_LOD_CLAMP_NONE;
    Check(vkCreateSampler(g.device, &hsci, nullptr, &g.hdSampler), "vkCreateSampler(hd)");
```

- [ ] **Step 4: Build**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -20`
Expected: links `linux/linuxxdoom` clean; `rb_image.o` compiled; no new warnings.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/Makefile linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0042: link rb_image/rb_materials; add HD linear+mip sampler

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: HD descriptor set + bindless RGBA8 PBR array with mip generation

**Files:**
- Modify: `linuxdoom-1.10/r_vulkan.cpp` (VulkanState fields; a new `descriptor set` + layout; a new method `UploadHdMaterials`)

**Interfaces:**
- Consumes: `g.hdSampler` (Task 6); the resolved `rb_matctrl_t` table + upload order (Tasks 3/5, wired in Task 9); decoded images (`rb_image_load`, Task 4).
- Produces:
  - `g.hdSetLayout`, `g.hdSet` — a **new descriptor set** (its own layout/pool) with **binding 0** = the control SSBO (Task 8) and **binding 1** = a variable-count `materialTex`-style RGBA8 image array (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, the variable-count binding, so it is the highest-numbered in the set — the reason it cannot be added to set 1, whose binding 2 is already variable-count).
  - `std::vector<VkImage> g.hdImages; std::vector<VkImageView> g.hdViews; VkDeviceMemory g.hdMemory;` — the PBR image array; each loaded map is one image with a full mip chain.
  - A method `int VulkanState::UploadHdMaps(const std::vector<HdMapUpload>& maps)` that creates/uploads the images and returns the count, assigning each `maps[i]` a slot index `i` (the value written into `rb_matctrl_t::maps[k]`).

**Note to implementer:** confirm the RT pipeline layout's current descriptor-set count where `vkCreatePipelineLayout` is called for the trace pipeline, and append `g.hdSetLayout` as the next set index. Bind `g.hdSet` at that index in `RecordRtTrace` (`r_vulkan.cpp:5677`) alongside the existing `g.ds`. The set index chosen here is the GLSL `set = N` in Task 10.

- [ ] **Step 1: Add the per-upload descriptor + state fields**

In `struct VulkanState`, add:
```cpp
    // DOOM-0042 HD PBR material array (parallel to the R8 matImages).
    struct HdMapUpload { const unsigned char* rgba; int w, h; bool srgb; };
    VkDescriptorSetLayout hdSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      hdPool      = VK_NULL_HANDLE;
    VkDescriptorSet       hdSet       = VK_NULL_HANDLE;
    std::vector<VkImage>     hdImages;
    std::vector<VkImageView> hdViews;
    VkDeviceMemory           hdMemory  = VK_NULL_HANDLE;
    VkBuffer                 hdCtrlBuf = VK_NULL_HANDLE;   // Task 8
    VkDeviceMemory           hdCtrlMem = VK_NULL_HANDLE;   // Task 8
    bool                     hdBuilt   = false;
```

- [ ] **Step 2: Create the HD set layout (binding 0 = SSBO, binding 1 = variable-count image array)**

Add a method `void CreateHdSetLayout(uint32_t maxImages)` modeled on the existing set-1 creation (`r_vulkan.cpp:3600-3634`), but with two bindings — binding 0 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` (stage `VK_SHADER_STAGE_COMPUTE_BIT`), binding 1 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` with `descriptorCount = maxImages` and the `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` flags on binding 1 only. Allocate `g.hdSet` from `g.hdPool` with a `VkDescriptorSetVariableDescriptorCountAllocateInfo` giving the actual image count. Reuse the exact flag/`pNext` pattern from the paletted array so the variable-count semantics match.

- [ ] **Step 3: Implement UploadHdMaps (images + mip chain via vkCmdBlitImage)**

Add `int UploadHdMaps(const std::vector<HdMapUpload>& maps)`:
1. For each map compute `mipLevels = floor(log2(max(w,h))) + 1`.
2. Create each `VkImage` — `format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM`, `mipLevels` as computed, `usage = TRANSFER_SRC | TRANSFER_DST | SAMPLED` (TRANSFER_SRC needed so each level can blit to the next).
3. Back all images with one device allocation (mirror the sub-allocator at `r_vulkan.cpp:4395-4429`).
4. Staging-upload level 0 (mirror `r_vulkan.cpp:4344-4389`, but 4 bytes/texel), then for each image: transition level 0 → TRANSFER_SRC, blit 0→1, 1→2 … with `VK_FILTER_LINEAR` (each level transitions SRC→ then serves as blit source), finally transition all levels → SHADER_READ. This is the standard Vulkan mip-gen loop; keep it in one command buffer via `BeginOneTime`/`EndOneTime`.
5. Create a view per image (`subresourceRange.levelCount = mipLevels`), push `{ g.hdSampler, view, SHADER_READ_ONLY_OPTIMAL }` into the binding-1 array-write `VkDescriptorImageInfo` list.
6. Return `maps.size()`; the caller records slot indices `[0, n)`.

Track cumulative bytes (`w*h*4 * 1.333...` for the mip chain) so Task 9's log can print the running MB.

- [ ] **Step 4: Build**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -20`
Expected: compiles clean. (No runtime path exercises it yet — Task 9 calls it.)

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0042: HD descriptor set + bindless RGBA8 PBR array with mip-gen

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Per-material control SSBO — create, populate, upload

**Files:**
- Modify: `linuxdoom-1.10/r_vulkan.cpp` (a `CreateHdCtrlBuffer` + populate/upload, binding 0 of the HD set)

**Interfaces:**
- Consumes: the `rb_matctrl_t table[nmaterials]` (Task 3/5) after image slots are assigned (Task 7 returns slot indices; write them into `table[id].maps[k]`).
- Produces: `g.hdCtrlBuf` — a device-local `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` buffer of `nmaterials × sizeof(rb_matctrl_t)` bytes, its contents = the table verbatim (the C struct is std430-identical, 40 bytes, asserted in Task 3). Written into HD-set binding 0.

- [ ] **Step 1: Create + upload the SSBO**

Add `void CreateHdCtrlBuffer(const rb_matctrl_t* table, int nmaterials)`:
1. `VkDeviceSize bytes = (VkDeviceSize)nmaterials * sizeof(rb_matctrl_t);` (add `static_assert(sizeof(rb_matctrl_t) == 40)` at file scope near the include).
2. Create a host-visible staging buffer, `memcpy` the table in, create the device-local `g.hdCtrlBuf` (`STORAGE_BUFFER | TRANSFER_DST`), `vkCmdCopyBuffer` staging→device in a one-time command buffer, free staging.
3. Write it into HD-set binding 0 with a `VkWriteDescriptorSet` (`descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `pBufferInfo = { g.hdCtrlBuf, 0, bytes }`).

Include `rb_materials.h` at the top of `r_vulkan.cpp` with a **plain** `#include "rb_materials.h"` — **not** inside `extern "C"`. It is a header-only C++-compatible header (`static inline` functions), like `emissive_derive.h`; wrapping it in `extern "C"` is wrong. (Contrast `rb_image.h`, which *is* a C-compiled object and self-guards its own `extern "C"`.)

- [ ] **Step 2: Build**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -20`
Expected: compiles clean; the `static_assert` holds (else the struct layout drifted — fix padding).

- [ ] **Step 3: Commit**

```bash
git add linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0042: per-material control SSBO (std430) create+upload

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Orchestrate the load — EnsureHdMaterials, tier gate, failure fallback, load log

**Files:**
- Modify: `linuxdoom-1.10/r_vulkan.cpp` (`EnsureHdMaterials`, called from `RB_Vulkan_BuildLevel:5550`)
- Modify: `linuxdoom-1.10/rb_materials.h` (append inline `rb_traffic_from_mesh` + `rb_asset_root`/`rb_asset_path` — header-only)

**Interfaces:**
- Consumes: `RB_MaterialCount()`, `R_CheckTextureNumForName`, `firstflat`/`numtextures`/`numflats`, `g.matNumWall`/`g.matNumFlat`, the level mesh `rb_vertex_t` array, `DOOMASSETDIR`.
- Produces: `void VulkanState::EnsureHdMaterials()` — idempotent; when `rendermode == RB_RT3D` and `!g.hdBuilt`, loads the current map's HD sets and sets `g.hdBuilt`. Wires the whole Phase-A/B chain together. On any per-material failure, that material stays paletted (`usePBR = 0`) and a one-line warning prints.

- [ ] **Step 1: Add the asset-root + traffic helpers to `rb_materials.h` (inline, before the final `#endif`)**

These are pure and belong with the other header-only logic. Add `#include <stdio.h>` to the header's include block at the top (for `snprintf`; `getenv` is already covered by `<stdlib.h>`).
```c
static inline const char* rb_asset_root(void) {          /* DOOMASSETDIR or "assets/ultra/" */
    const char* e = getenv("DOOMASSETDIR");
    return (e && *e) ? e : "assets/ultra/";
}
static inline void rb_asset_path(char* dst, int dstsz, const char* rel) {  /* root + '/' + rel */
    const char* root = rb_asset_root();
    size_t n = strlen(root);
    int need_slash = (n > 0 && root[n-1] != '/');
    snprintf(dst, dstsz, "%s%s%s", root, need_slash ? "/" : "", rel);
}

/* Accumulate world surface area per unified material id from the rb_vertex_t soup.
   stride_floats = 18; pos at [0..2]; texnum at [8] (int bits); flags at [9] (int bits).
   flag_flat = the FLAG_FLAT bit; num_wall/num_flat map texnum -> unified id. */
static inline void rb_traffic_from_mesh(const float* verts, int nverts, int stride_floats,
                          unsigned int flag_flat, int num_wall, int num_flat,
                          int nmaterials, float* traffic_out) {
    for (int i = 0; i < nmaterials; i++) traffic_out[i] = 0.0f;
    int ntri = nverts / 3;
    for (int t = 0; t < ntri; t++) {
        const float* v0 = verts + (size_t)(t * 3 + 0) * stride_floats;
        const float* v1 = verts + (size_t)(t * 3 + 1) * stride_floats;
        const float* v2 = verts + (size_t)(t * 3 + 2) * stride_floats;
        /* area = 1/2 |(p1-p0) x (p2-p0)| */
        float ax = v1[0]-v0[0], ay = v1[1]-v0[1], az = v1[2]-v0[2];
        float bx = v2[0]-v0[0], by = v2[1]-v0[1], bz = v2[2]-v0[2];
        float cx = ay*bz - az*by, cy = az*bx - ax*bz, cz = ax*by - ay*bx;
        float area = 0.5f * (float)sqrt((double)(cx*cx + cy*cy + cz*cz));
        /* unified id from vertex 0's texnum/flags — exactly pathtrace.comp:439-440 (world path). */
        int texnum, iflags;
        memcpy(&texnum, &v0[8], sizeof(int));
        memcpy(&iflags, &v0[9], sizeof(int));
        int id = ((unsigned)iflags & flag_flat) ? (num_wall + texnum) : texnum;
        if (id >= 0 && id < nmaterials) traffic_out[id] += area;
    }
}
```
(`sqrt` needs `<math.h>` — add it to the header's includes. `memcpy` reads the int-bit-pattern of the `float` slots the way the shader's `floatBitsToInt` does. This mirrors the world-surface arm only; sprites are excluded from v1 HD, so their traffic is irrelevant.)

- [ ] **Step 2: Write EnsureHdMaterials**

In `r_vulkan.cpp`:
```cpp
void EnsureHdMaterials() {
    if (rendermode != RB_RT3D || g.hdBuilt) return;

    const int N = RB_MaterialCount();

    // 1. Load the sidecar (no file => everything paletted; not an error).
    char csvPath[512];
    rb_asset_path(csvPath, sizeof(csvPath), "materials.csv");
    FILE* f = fopen(csvPath, "r");
    if (!f) { printf("DOOM-0042: no %s — Ultra uses paletted art.\n", csvPath); g.hdBuilt = true; return; }

    std::vector<rb_matrow_t> rows;
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        rb_matrow_t r;
        int rc = rb_parse_material_line(line, &r);
        if (rc == 1) rows.push_back(r);
        else if (rc == -1) printf("DOOM-0042: %s:%d malformed row — skipped.\n", csvPath, lineno);
    }
    fclose(f);

    // 2. Resolve names -> unified ids into the control table.
    std::vector<rb_matctrl_t> table(N);
    int dups = 0;
    rb_build_ctrl_table(rows.data(), (int)rows.size(), N, &ResolveDoomName, table.data(), &dups);
    if (dups) printf("DOOM-0042: %d duplicate doom_name row(s) — last wins.\n", dups);

    // 3. Traffic + hero flags per id (for the budget). is_hero from the matching row.
    std::vector<float> traffic(N, 0.0f);
    rb_traffic_from_mesh(/*verts*/ g.levelVerts.data(), /*nverts*/ (int)g.levelVerts.size()/18, 18,
                         FLAG_FLAT, g.matNumWall, g.matNumFlat, N, traffic.data());
    std::vector<int> isHero(N, 0);
    for (auto& r : rows) { int id; if (ResolveDoomName(r.name, &id) && id < N) isHero[id] = r.is_hero; }

    // 4. Decode every map of every HD material; estimate MB; build the upload list.
    //    A material with ANY undecodable/missing map it needs falls back to paletted.
    struct Pending { int id; std::vector<VulkanState::HdMapUpload> maps; std::vector<int> mapSlot; float mb; };
    // (Resolve each map path: hero -> row cell; derive -> derived/<name>_<suffix>.png.
    //  suffix order alb,nrm,rgh,met,ao,emis,hgt; v1 loads alb,nrm,ao,emis,hgt only.
    //  srgb = (k==RB_ALB || k==RB_EMIS).)
    //  ... decode via rb_image_load + rb_image_downscale_max(&img, 1024); on failure log
    //      and set table[id].usePBR = 0, skip the material.

    // 5. Budget: est_mb[id] = sum of that material's decoded map MB (with mip 1.333x).
    //    rb_apply_budget(...) drops lowest-traffic over 768 MB to paletted, returns order.

    // 6. Concatenate the kept materials' maps in upload order, UploadHdMaps(), then write
    //    the returned slot indices into table[id].maps[k]; -1 for maps not uploaded
    //    (roughness/metallic always -1 in v1).

    // 7. CreateHdSetLayout(totalImages); CreateHdCtrlBuffer(table.data(), N); bind the set.

    // 8. Load log: per material -> resolution + running MB; list downscaled/dropped/skipped.
    g.hdBuilt = true;
}
```
Fill in steps 4–8 concretely against the Task 7/8 methods. Add a file-scope `static int ResolveDoomName(const char* name, int* out_id)`:
```cpp
static int ResolveDoomName(const char* name, int* out_id) {
    char n[9]; strncpy(n, name, 8); n[8] = 0;
    int t = R_CheckTextureNumForName(n);
    if (t >= 0) { *out_id = t; return 1; }                 // wall
    int lump = W_CheckNumForName(n);
    if (lump >= 0) {
        int flatIdx = lump - firstflat;
        if (flatIdx >= 0 && flatIdx < numflats) { *out_id = g.matNumWall + flatIdx; return 1; }  // flat
    }
    return 0;                                               // not in this WAD (or a sprite: v1 skips)
}
```
(`g.levelVerts` = whatever CPU copy of the level mesh already exists; if none is retained, capture it in `RB_Vulkan_BuildLevel` where the mesh is uploaded. Confirm the retained buffer's name/stride against `RB_BuildLevelMesh`.)

- [ ] **Step 3: Call it from the level-build hook + on entering Ultra**

In `RB_Vulkan_BuildLevel` (`r_vulkan.cpp:5550`), after the mesh/BLAS are built, call `g.EnsureHdMaterials();`. Also invalidate on level change: set `g.hdBuilt = false` and free prior HD resources at the *start* of `RB_Vulkan_BuildLevel` (a new map's texnums differ). Add a small `FreeHdMaterials()` that destroys `hdImages/hdViews/hdMemory/hdCtrlBuf/hdCtrlMem/hdPool` and clears `hdBuilt`.

- [ ] **Step 4: Build + run E1M1 in Ultra, read the load log**

Run: `cd linuxdoom-1.10 && make && DOOMWADDIR=../wads DOOMASSETDIR=../assets/ultra ./linux/linuxxdoom -iwad doom1.wad 2>&1 | grep -i "DOOM-0042\|bindless\|MB" | head -40`
Expected: the load log lists STARTAN3 loaded at its resolution with a running MB total under 768; TEKWALL1/FLOOR4_8 are `derive` rows whose `derived/*.png` don't exist yet → logged as skipped-to-paletted (that is correct until Task 16). No crash; map loads.
*(If launching interactively is easier, use the panel launcher; select Ultra, load E1M1, and read the konsole log.)*

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/r_vulkan.cpp linuxdoom-1.10/rb_materials.h
git commit -m "DOOM-0042: orchestrate HD load (tier gate, traffic budget, fallback, log)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Shader — bind the HD set + usePBR albedo branch  ← FIRST VISIBLE RESULT

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` (bindings near `:58-66`; the `mode == 3u` textured path `:426-446`)

**Interfaces:**
- Consumes: the HD descriptor set (Task 7, at the set index chosen there), the control SSBO (Task 8), the unified id `id` already computed at `pathtrace.comp:439`.
- Produces: HD materials sample RGBA albedo directly (skip the palette LUT); paletted materials unchanged.

- [ ] **Step 1: Declare the HD set bindings**

After the material bindings (`pathtrace.comp:66`), add (use the set index from Task 7 — shown here as `set = 3`):
```glsl
// DOOM-0042 HD PBR material set.
struct MatCtrl {
    int  maps[7];     // [0]alb [1]nrm [2]rgh [3]met [4]ao [5]emis [6]hgt; -1 = none
    float uvScale;
    uint flags;       // bit0 pom, bit1 noPom, bit2 sprite
    uint usePBR;      // 0 = paletted fallback
};
layout(set = 3, binding = 0, std430) readonly buffer MatCtrlBuf { MatCtrl ctrl[]; };
layout(set = 3, binding = 1) uniform sampler2D hdTex[];   // bindless RGBA8 PBR maps
```

- [ ] **Step 2: Branch on usePBR in the textured path**

In the `mode == 3u` block (`pathtrace.comp:426`), after `id` is computed (`:439-440`) and `hitUV` is available, replace the single-line paletted albedo (`:441-445`) with a branch:
```glsl
            MatCtrl mc = ctrl[nonuniformEXT(id)];
            vec3 albedo;
            if (mc.usePBR != 0u && mc.maps[0] >= 0) {
                vec2 uv = hitUV * mc.uvScale;                 // uv_scale first (POM in Task 14)
                albedo = texture(hdTex[nonuniformEXT(mc.maps[0])], uv).rgb;  // sRGB view -> linear
            } else {
                vec2  sz  = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
                float idx = texture(materialTex[nonuniformEXT(id)], hitUV / sz).r * 255.0;
                albedo = texture(paletteTex, vec2((idx + 0.5) / 256.0, 0.5)).rgb;
            }
            colour = albedo * light;
```
(The sRGB image *view format* does the gamma→linear decode in hardware, so no `pow(2.2)` here — unlike the paletted `decodeAlbedo`.)

**Note:** the real textured shading for the shipped Ultra view is `mode == 4u` (NEE, `pathtrace.comp:447`), not `mode == 3u`. Confirm which mode the Ultra RT default (`rb_rtdebug == 6`) dispatches (grep `misc.x` / the `mode` push in `RecordRtTrace`), and apply the same `usePBR` albedo branch there — factor it into a helper `vec3 hdAlbedo(uint id, vec2 hitUV, out MatCtrl mc)` in `pt_common.glsl` so both the `mode==3` seed and the `mode==4` NEE path share one decode. Later tasks (11–14) extend the same helper.

- [ ] **Step 3: Build the shader + engine**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -20`
Expected: `glslc` compiles `pathtrace.comp` with 0 warnings; engine links.

- [ ] **Step 4: Visual check — hero albedo on E1M1**

Launch Ultra, load E1M1, look at a STARTAN3 wall (the tech-base walls near the start).
Expected: the STARTAN3 wall shows the bring-up hero's blue-grey checker tint (Task 1) instead of the paletted DOOM texture — proving the HD albedo path is live. Everything else stays paletted. Switch to Solid → original art returns.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0042: usePBR albedo branch in the path tracer (first HD pixels)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Shader — normal mapping with ∂P/∂U,∂P/∂V tangent frame

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` / `pt_common.glsl` (extend the HD helper)

**Interfaces:**
- Consumes: `mc` + `hitUV` + the geometric normal `n` + the three hit-triangle vertex positions & UVs (already decoded around `pathtrace.comp:394-408`).
- Produces: a world-space shading normal replacing the flat geometric normal when `mc.maps[1] >= 0`.

- [ ] **Step 1: Compute the tangent frame from triangle edges + UV deltas**

At the hit decode, gather the three vertices' positions (`vb.v[i0+0..2]`, `+18..20`, `+36..38`) and UVs (already have `uv0,uv1,uv2` from the barycentric block). Add to the HD helper:
```glsl
// Tangent + bitangent from dP/dU and dP/dV (carry the mirror sign for X-flipped walls).
vec3 e1 = p1 - p0, e2 = p2 - p0;
vec2 d1 = uv1 - uv0, d2 = uv2 - uv0;
float det = d1.x * d2.y - d1.y * d2.x;
vec3 T, B;
if (abs(det) < 1e-8) {                       // degenerate UVs -> arbitrary frame
    T = normalize(abs(n.z) < 0.999 ? cross(vec3(0,0,1), n) : cross(vec3(1,0,0), n));
    B = cross(n, T);
} else {
    float r = 1.0 / det;
    T = normalize((e1 * d2.y - e2 * d1.y) * r);
    B = normalize((e2 * d1.x - e1 * d2.x) * r);
    T = normalize(T - n * dot(n, T));        // Gram-Schmidt against the geometric normal
    B = cross(n, T) * sign(det);             // restore handedness / mirror sign
}
```

- [ ] **Step 2: Unpack + rotate the tangent-space normal (OpenGL Y+)**

```glsl
vec3 shadeN = n;
if (mc.usePBR != 0u && mc.maps[1] >= 0) {
    vec3 tn = texture(hdTex[nonuniformEXT(mc.maps[1])], hitUV * mc.uvScale).xyz * 2.0 - 1.0; // Y+ (no flip)
    shadeN = normalize(tn.x * T + tn.y * B + tn.z * n);
}
```
Use `shadeN` for all shading (NEE cosine, GI, flashlight) in place of `n` in the HD path; keep `n` for the ray-offset/geometric terms.

- [ ] **Step 3: Build**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -10`
Expected: 0 shader warnings; links.

- [ ] **Step 4: Visual check — normal-driven shading**

On a normal-mapped wall (once curated heroes land the effect is strong; the flat bring-up normal shows no change, which is the correct null result — verify against a derived material after Task 16, or temporarily point STARTAN3's `nrm` at a non-flat map). Take a screenshot pair at two camera angles with geometry fixed: shading varies with view/light, proving the normal map drives it.

- [ ] **Step 5: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0042: tangent-space normal mapping (dP/dU,dP/dV frame, Y+)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: Shader — ambient occlusion on the indirect term only

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` / `pt_common.glsl`

**Interfaces:**
- Consumes: `mc` + `hitUV`; the code path where the ambient/GI-bounce contribution is added (distinct from the direct NEE/flashlight term).
- Produces: crevices darken under ambient light; flashlight-lit surfaces are not dimmed.

- [ ] **Step 1: Sample AO and multiply into the ambient term**

```glsl
float ao = 1.0;
if (mc.usePBR != 0u && mc.maps[4] >= 0)
    ao = texture(hdTex[nonuniformEXT(mc.maps[4])], hitUV * mc.uvScale).r;   // single-channel .r
```
Locate where the GI-bake bounce + sky/sector ambient are accumulated (grep the probe/`ambient` term in the NEE path). Multiply **that** term by `ao`. **Do not** multiply the direct NEE/flashlight contribution by `ao`.

- [ ] **Step 2: Build + visual check**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -5`
Ultra E1M1: corners/recesses on an AO-bearing material read darker under ambient; sweeping the flashlight directly onto the same spot still fully lights it (AO doesn't fight the torch).

- [ ] **Step 3: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0042: AO on the ambient/GI term only (not direct NEE)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: Shader — primary-hit emissive

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` / `pt_common.glsl`

**Interfaces:**
- Consumes: `mc` + `hitUV`; a "is this the primary (camera) hit?" flag (the first trace, before any bounce).
- Produces: emissive materials add self-radiance on the primary hit; not registered as NEE emitters in v1.

- [ ] **Step 1: Add emissive on the primary hit**

Near the file-scope constants, add `const float kEmissiveScale = 40.0;` (reused value from `emissive_derive.h:52`; re-tune expected — lower). Where the primary hit's outgoing radiance is composed:
```glsl
if (isPrimaryHit && mc.usePBR != 0u && mc.maps[5] >= 0) {
    vec3 emis = texture(hdTex[nonuniformEXT(mc.maps[5])], hitUV * mc.uvScale).rgb;  // sRGB->linear
    colour += emis * kEmissiveScale;
}
```
Confirm the megakernel's notion of the primary hit (the first `RecordRtTrace` bounce / depth 0). If the shipped integrator has no explicit primary flag, gate on bounce depth `== 0`.

- [ ] **Step 2: Build + visual check**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -5`
Ultra E1M1: a material with an emissive map (a lit computer panel once derived maps land, Task 16) glows; ordinary walls stay dark. If glow blows out, lower `kEmissiveScale` (re-tune is expected).

- [ ] **Step 3: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0042: primary-hit emissive (kEmissiveScale, re-tune expected)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: Shader — parallax occlusion mapping (option a, primary-hit)

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` / `pt_common.glsl`

**Interfaces:**
- Consumes: `mc` (needs `flags:pom` and `maps[6] >= 0`), the tangent frame `T,B,n` (Task 11), the view direction `dir`, the scaled UV.
- Produces: a UV offset applied to **all** subsequent map samples (albedo/normal/AO/emissive) so relief recesses and self-occludes in the primary-hit shade.

- [ ] **Step 1: Compute the POM'd UV before sampling any map**

Insert this **before** the albedo/normal/AO/emissive samples (they must all use `pomUV`). White = raised, so march *into* the surface along the view vector in tangent space:
```glsl
const float kPomHeightScale = 0.06;              // fraction of one UV tile
vec2 pomUV = hitUV * mc.uvScale;
bool doPom = (mc.usePBR != 0u) && ((mc.flags & 1u) != 0u) && (mc.maps[6] >= 0);  // bit0 = pom
if (doPom) {
    // View direction in tangent space (T,B,n orthonormal from Task 11).
    vec3 vT = normalize(vec3(dot(-dir, T), dot(-dir, B), dot(-dir, n)));
    float nv = clamp(vT.z, 0.05, 1.0);
    int steps = int(mix(32.0, 16.0, nv));        // 16 at normal incidence -> 32 grazing
    // Parallax offset spans at most kPomHeightScale of a tile, bounded (NOT UV-clamped).
    vec2 maxOff = (vT.xy / nv) * kPomHeightScale;
    float layer = 1.0 / float(steps);
    float curH = 0.0, prevH = 0.0;
    vec2  curUV = pomUV, prevUV = pomUV;
    float curD = 0.0, prevD = 0.0;
    for (int i = 0; i < steps; i++) {
        prevUV = curUV; prevH = curH; prevD = curD;
        curD += layer;                           // marched depth 0..1
        curUV = pomUV - maxOff * curD;           // offset bounded by maxOff (REPEAT sampler wraps)
        curH = textureLod(hdTex[nonuniformEXT(mc.maps[6])], curUV, 0.0).r;  // LOD 0, white=raised
        if (curD >= (1.0 - curH)) break;         // ray dipped below the height field
    }
    // One bisection refine between prev (above) and cur (below).
    float h0 = (1.0 - prevH) - prevD;
    float h1 = (1.0 - curH)  - curD;
    float t  = h0 / max(h0 - h1, 1e-5);
    pomUV = mix(prevUV, curUV, clamp(t, 0.0, 1.0));
}
```
Then feed `pomUV` (not `hitUV * mc.uvScale`) into the Task 10/11/12/13 samples. `noPom` and non-pom materials keep `pomUV = hitUV * mc.uvScale` (no march).

- [ ] **Step 2: Build**

Run: `cd linuxdoom-1.10 && make 2>&1 | tail -5`
Expected: 0 shader warnings.

- [ ] **Step 3: Visual check — parallax**

Ultra E1M1, a `flags:pom` wall with a height map (STARTAN3's ramp height, or a derived pom wall after Task 16): a screenshot pair at two view angles shows grooves shift/deepen without swimming; at grazing angles no gaping holes / no garbage tiling (the offset-bound + step ramp hold). A visible hole is the trigger to reconsider option (c) — not to ship.

- [ ] **Step 4: Commit**

```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/shaders/pt_common.glsl
git commit -m "DOOM-0042: parallax occlusion mapping (option a, offset-bounded REPEAT)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: Regression gate — Solid/Classic byte-identical; coexistence

**Files:** none (verification task).

- [ ] **Step 1: Off-Ultra frame-diff**

With HD live on E1M1, switch to **Solid** and **Classic**. Capture a frame in each and compare against a pre-DOOM-0042 build's frame at the same position (or reason from the gate: every HD read is under `rendermode == RB_RT3D` and `mc.usePBR`, so Solid/Classic never touch `hdTex`/`ctrl`). Confirm no HD code executes off-Ultra.
Expected: Solid/Classic pixel-identical to today.

- [ ] **Step 2: Coexistence in one frame**

On E1M1 in Ultra, confirm a frame containing a hero material (STARTAN3), a still-paletted material (any wall with no CSV row), and — after Task 16 — a derived material all render correctly together (the `usePBR` branch per material).

- [ ] **Step 3: Full build + test sweep**

Run: `cd linuxdoom-1.10 && make && make test`
Expected: clean build, all unit tests pass, no new warnings.

- [ ] **Step 4: Commit (if any doc/gate tweak was needed; otherwise skip)**

---

## Task 16: Offline derive generator — fill E1M1's long tail

**Files:**
- Create: `scripts/pbr_derive.py`

**Interfaces:**
- Consumes: `doom1.wad` (lump directory + PLAYPAL + TEXTURE1/PNAMES for composited walls + raw flats) and `assets/ultra/materials.csv` (the `derive` rows).
- Produces: `assets/ultra/derived/<doom_name>_<suffix>.png` for each derive row (`suffix ∈ alb,nrm,rgh,met,ao,emis,hgt`) — the files the loader resolves by convention. Gitignored output.

**Note:** this is a standalone offline tool, **not** built into the engine. It reads the WAD with its own Python lump/PLAYPAL/texture parser (the engine's C `W_CacheLumpName` isn't reusable offline). It is large but self-contained; write it in these stages, each runnable.

- [ ] **Step 1: WAD reader — lump directory + PLAYPAL + flat/texture pixels**

Write `scripts/pbr_derive.py` with a `Wad` class: parse the 12-byte header (`IWAD`/`PWAD`, numlumps, dirofs), the lump directory (`filepos, size, name[8]`), `PLAYPAL` (first 768 bytes = 256 RGB), flats (raw 64×64 palette indices between `F_START`/`F_END`), and composited wall textures via `PNAMES` + `TEXTURE1` (patch assembly: for each texture, blit its patches — column-based `patch_t` format — into a `width×height` palette-index buffer). Expose `texture_rgb(name)` / `flat_rgb(name)` → an RGB numpy-free `bytearray` + `(w,h)` using PLAYPAL. Verify on one known texture:
```
python3 scripts/pbr_derive.py --dump STARTAN3 --wad ../wads/doom1.wad
```
Expected: writes a debug `STARTAN3_dump.png` you can eyeball as the correct DOOM texture.

- [ ] **Step 2: Map derivation from an albedo image**

Add functions taking the RGB albedo → each PBR map (reuse the `write_png` helper from Task 1, extended to grayscale + RGBA as needed):
- **height** = per-texel luminance (`0.299r+0.587g+0.114b`), **brighter = raised**.
- **normal** = Sobel gradient of height, strength ×2.0, encoded **OpenGL Y+** (`rgb = (−gx, −gy, 1)` normalized → `*0.5+0.5`; keep `n.y` un-flipped to match the shader's Y+).
- **ao** = horizon-based local occlusion of the height field, 4-px radius (sample N directions, accumulate the fraction of horizon blocked).
- **roughness/metallic** = flat fills from the family-prefix table (longest matching `doom_name` prefix):

  | Prefix family | roughness | metallic |
  |---|---|---|
  | `METAL TEK SILVER SHAWN SUPPORT` | 0.35 | 1.0 |
  | `BROWN BRONZE COMP PIPE` | 0.55 | 1.0 |
  | `BRICK STONE ROCK GRAY MARB` | 0.85 | 0.0 |
  | `WOOD PANEL DOOR` | 0.75 | 0.0 |
  | `FLOOR FLAT CEIL RROCK MFLR` | 0.80 | 0.0 |
  | *(default)* | 0.70 | 0.0 |

- **emissive** = black except near-fullbright/known-emissive texels — reuse the `emissive_derive.h` peak-luminance gate (`kEmitterPeakLum`) logic in Python: a texel whose luminance clears the peak gate keeps its albedo colour in the emissive map; all others are black.
- For `sprite`-flagged rows (none in v1), carry the WAD's index-0 transparency into the albedo alpha channel.

- [ ] **Step 3: Drive from the CSV — emit all seven maps per derive row**

Parse `materials.csv`, and for every `source == derive` row, load its WAD image and write the seven `derived/<name>_<suffix>.png`. Run it:
```
python3 scripts/pbr_derive.py --wad ../wads/doom1.wad --csv assets/ultra/materials.csv --out assets/ultra/derived
ls assets/ultra/derived/ | head
```
Expected: `TEKWALL1_alb.png … TEKWALL1_hgt.png`, `FLOOR4_8_*.png`, etc. (7 files per derive row).

- [ ] **Step 4: Verify in-engine — E1M1 long tail now HD**

Run the engine on E1M1 in Ultra again.
Expected: the previously-paletted `derive` materials (TEKWALL1, FLOOR4_8) now load HD (the load log shows them loaded, not skipped); the map reads as HD across walls+flats, keeping the DOOM look but with bump/roughness/relief depth. Derived pom walls now exercise Task 14's march.

- [ ] **Step 5: Commit (script only — derived/ is gitignored)**

```bash
git add scripts/pbr_derive.py
git commit -m "DOOM-0042: offline pbr_derive.py generator (WAD -> PBR map set)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 17: Curated CC0 heroes + provenance + final slice verification

**Files:**
- Modify/Create: `assets/ultra/heroes/**` (curated CC0 sets), `assets/ultra/materials.csv` (point hero rows at them), `assets/ultra/LICENSES`
- Remove: `assets/ultra/heroes/bringup/**` and `scripts/make_bringup_hero.py` (bring-up scaffolding, once real heroes exist)

**Note:** picking the CC0 sets is a collaborative step with the user (their `/mnt/Games/3D Engine Assets/` library + ambientCG/Poly Haven). Stage 1–2 dozen high-traffic DOOM names (STARTAN/BROWN/TEKWALL tech panels, brick, metal, floors) → CC0 sets, copy the used maps into `assets/ultra/heroes/`, record each in `LICENSES`.

- [ ] **Step 1: Stage curated hero sets**

With the user, select CC0 sets for the highest-traffic E1M1 surfaces. Copy each map (albedo/normal/ao/height; roughness/metallic optional, unused until DOOM-0103) into `assets/ultra/heroes/<family>/`, downscaled to ≤1024 if larger. Add hero rows / update existing ones in `materials.csv`.

- [ ] **Step 2: Record provenance**

Append one `LICENSES` line per hero PNG: `<path>  <source-url>  CC0`.

- [ ] **Step 3: Retire the bring-up scaffolding**

Remove `heroes/bringup/` and `scripts/make_bringup_hero.py`; drop the bring-up `STARTAN3` hero row's placeholder paths (repoint STARTAN3 at a curated set or make it a `derive` row).

- [ ] **Step 4: Full slice verification (spec §Verification)**

Ultra RT view, E1M1 — walk the map and confirm:
- Hero surfaces read as modern PBR (matte concrete, relief on brick/tech).
- Normal/relief: screenshot pair at two angles, geometry fixed, shading changes.
- Parallax: `pom` wall grooves shift/deepen without swimming; grazing angles hold.
- Coexistence: hero + derive + paletted all correct in one frame.
- Memory/load: log shows all maps ≤1024 px, running MB < 768, nothing silently truncated.
- Hard gate: Solid/Classic pixel-identical.
- Specular check is **N/A** until DOOM-0103 (roughness/metallic have no visible effect yet — expected).
- Licence: every hero traces to CC0 in `LICENSES`; no WAD-derived art committed (derived/ gitignored).

Run: `cd linuxdoom-1.10 && make && make test` → clean.

- [ ] **Step 5: Commit + roadmap/changelog**

```bash
git add assets/ultra
git rm -r assets/ultra/heroes/bringup scripts/make_bringup_hero.py
git commit -m "DOOM-0042: curated CC0 hero materials + E1M1 slice verification

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```
Then flip the DOOM-0042 roadmap bullet's slice status and add a CHANGELOG entry (the E1M1 HD-PBR slice, walls+flats, RT view; roughness/metallic gated on DOOM-0103).

---

## Self-Review

**Spec coverage** (each spec section → task):
- §A sidecar format → T1 (file) + T2 (parser). Derive naming convention → T9 (resolution) + T16 (generator writes them). Hero staging → T1 (bring-up) + T17 (curated). Derive generator (WAD read, height/normal/AO/family table/emissive/sprite-alpha) → T16. Derive-out-of-repo → T1 (.gitignore). Licence hygiene → T1 + T17 (LICENSES).
- §B PNG loader (stb, no link dep) → T4. DOOMASSETDIR → T9. Load-current-map + resolution clamp + memory ceiling + traffic order + drop-to-paletted → T5 (logic) + T9 (wiring + log). Failure fallback / no-CSV / dup-name → T2 (malformed) + T9 (orchestration). Parallel RGBA8 array + sRGB/linear + mipgen + linear sampler + control SSBO (std430, maps[7], v1 5-map upload, rough/metal −1) → T6 (sampler) + T7 (array) + T8 (SSBO).
- §C usePBR branch → T10. POM (option a, offset-bounded, 16→32+bisection, white=raised, LOD0, maps[6] guard, noPom skip, uv_scale first) → T14. Albedo (skip palette) → T10. Normal (Y+, ∂P/∂U & ∂P/∂V, Gram-Schmidt, degenerate fallback) → T11. Roughness/metallic → DOOM-0103 (out of this plan; maps baked in T16, upload deferred — noted in T7/T8). AO on ambient only → T12. Emissive primary-hit, kEmissiveScale=40 re-tune → T13.
- §D tier hook (RB_RT3D gate, load-on-enter, leave→paletted) → T9 + T15.
- §Verification → T15 (regression/coexistence) + T17 (full slice). §Out-of-scope (HD sprites, raster-view POM, specular, compression) → respected (no tasks; sprite plumbing only).

**Placeholder scan:** GPU-plumbing tasks (T7/T8/T9 steps 3–8) describe method bodies by mirroring cited existing code (`r_vulkan.cpp:4344-4495`) rather than re-emitting hundreds of lines of boilerplate — the anchors + the concrete deltas (formats, bindings, mip loop) are given. This is deliberate: the exact Vulkan handles must be written against live code, and copying the sub-allocator/staging pattern verbatim into the plan would be a less-reliable duplicate than pointing at the source of truth. Every *pure-logic* and *shader* task carries complete code. No "TODO"/"handle edge cases"/"add validation" placeholders remain.

**Type consistency:** `rb_matctrl_t` (C, T3) ↔ `MatCtrl` (GLSL, T10) — both `int maps[7]; float uvScale; uint flags; uint usePBR;`, 40 bytes, `static_assert` in T3 + T8. Map index enum `RB_ALB..RB_HGT` (T2) = CSV order = `maps[0..6]` = GLSL `mc.maps[0..6]`. Flag bits `RB_FLAG_POM=1/NOPOM=2/SPRITE=4` (T2) = GLSL `flags & 1u/2u/4u` (T14). `rb_name_resolver_t` (T3) = `ResolveDoomName` (T9). Unified id math identical in `rb_traffic_from_mesh` (T9), `ResolveDoomName` (T9), and `pathtrace.comp:439-440`.

**Known cross-task confirmations the implementer must make against live code (flagged in-task, not gaps):** the RT pipeline-layout set count → HD set index (T7 → GLSL `set=N` in T10); which `mode` the Ultra RT default dispatches (T10, apply the branch to `mode==4` NEE too); the retained CPU level-mesh buffer name/stride for traffic (T9); the primary-hit/bounce-depth flag for emissive (T13); the ambient-vs-direct accumulation site for AO (T12). Each is named where it occurs.
