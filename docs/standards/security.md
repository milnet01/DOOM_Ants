# Security & Untrusted Input Standard

DOOM loads data made by other people — WAD files, mods, network peers. The
original 1997 code assumed that data was local and trusted, so many of its
parsers read first and ask questions never. On a modern machine, downloading a
PWAD off the internet is normal, so **that data is untrusted** and every parser
that touches it is a trust boundary.

This is the umbrella theme tracked as **DOOM-0093**; the 2026-07-23 hardening
pass (DOOM-0212…0220) is the worked example.

## The trust boundaries

Untrusted bytes enter the engine at these points. Anything reading them must
validate before it trusts:

| Input | Where it's parsed | The risk |
|-------|-------------------|----------|
| WAD directory & lumps | `w_wad.c` | lump count / offsets / lengths drive reads and allocations |
| In-WAD music (MUS→MIDI) | `mus2mid.c` | a header can over-declare its own size and read past the lump |
| Netgame packets | `i_net.c`, `d_net.c` | packet fields (`numtics`, `player`) index fixed arrays |
| Command line & `@response` files | `d_main.c` | arg counts, `-warp` operands, response-file tokens |
| User config `~/.doomrc` | `m_misc.c`, `m_menu.c` | out-of-range values index name/option arrays |
| Save games (`.dsg`) | `p_saveg.c` | serialized state read back into structs |

## The rule

**Bound every value that came from untrusted data before you use it.** In
practice:

- **Never trust a self-declared size.** Bound a header's length against the
  *actual* buffer size, not the number the header claims (the `mus2mid` fix:
  thread the real lump length in, clamp to `min`).
- **Bound every index to its array** before indexing (`netconsole` against
  `MAXPLAYERS`, `numtics` against `BACKUPTICS`, config values to their table
  range).
- **Check return values** of `read`/`fread`/`ftell` and allocations — a short
  read or a failed `ftell` must not become a wild size.
- **Use `snprintf`**, not `sprintf`, for anything interpolating outside data
  (env vars, paths).

Repairing missing checks in the 1997 code is **in scope** — it is the same kind
of modernisation as fixing 64-bit integer sizes, not a rewrite. Match the
surrounding style, and reference the trust boundary in a comment so the check
reads as deliberate.

## GPU path

Untrusted counts and IDs that reach the Vulkan path tracer (emitter counts,
bindless texture IDs, subsector indices) must be bounded **host-side** before
they go into a push-constant or buffer — the shader trusts what the host hands
it. The RT path additionally carries the `-rtverify` self-test (see the renderer
standard) as a correctness guard.

## When you find a hole

1. **Roadmap it** as `Kind: security` (or `audit-fix`) so it is tracked, even if
   you fix it immediately.
2. **Add a regression test** proving the crafted input is now rejected, not
   read out of bounds (see the testing standard).
3. **Log false positives.** A finding you verify is *not* a bug gets recorded
   (`.ants_review_falsepos.jsonl` / `audit_falsepos_log`) with the reason, so the
   next audit doesn't re-raise it.

Netgame is opt-in peer-to-peer, not a listening service — but a malicious or
spoofed peer is still untrusted, so its packets get the same treatment.
