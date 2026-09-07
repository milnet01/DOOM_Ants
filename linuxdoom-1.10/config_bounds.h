// config_bounds.h — DOOM-0383: reading a value out of a hand-edited config.
//
// ~/.doomrc is a plain text file the game writes and the user edits, so every
// number in it is untrusted input (docs/standards/security.md). Two things go
// wrong on the way from a line of that file to a global:
//
//   1. The value never parsed. Vanilla called sscanf and ignored its return,
//      into a `parm` that was never initialised -- so a malformed line left the
//      previous line's value in place, or, on the first line of the file,
//      whatever happened to be on the stack. That is how garbage reached
//      settings that are used as array indices.
//
//   2. The value parsed but is out of range. DOOM-0254 clamped four of these;
//      DOOM-0383 is the rest of the table. Ten key_* settings index
//      gamekeydown[NUMKEYS] every tic, mouseb_*/joyb_* index four- and
//      five-entry arrays, and the two volumes reach an I_Error that refuses to
//      start the game -- which is unrecoverable from a text file, because the
//      menu is how you would have fixed it.
//
// Both decisions live here, rather than inline in m_misc.c, so
// tests/config_bounds_test.cpp can hold the boundary cases against them with no
// config file and no globals -- the same reason level_bounds.h, wad_bounds.h,
// save_bounds.h, net_bounds.h and render_bounds.h exist.
#ifndef CONFIG_BOUNDS_H
#define CONFIG_BOUNDS_H

#include <stdio.h>

// Parse one config value. Returns 1 and writes *out on success, 0 on a
// malformed value -- in which case *out is 0 rather than indeterminate, so a
// caller that ignores the return still cannot propagate stack garbage.
//
// "0x" selects hex, matching what M_SaveDefaults writes for key bindings.
// Everything else goes through %i, which accepts decimal and octal.
static int ConfigParseInt (const char* text, int* out)
{
    unsigned int	hex;

    *out = 0;

    if (!text || !text[0])
	return 0;

    if (text[0] == '0' && text[1] == 'x')
    {
	// A bare "0x" has no digits and must not read as zero-and-valid.
	if (!text[2])
	    return 0;

	if (sscanf (text + 2, "%x", &hex) != 1)
	    return 0;

	*out = (int) hex;
	return 1;
    }

    return sscanf (text, "%i", out) == 1;
}

// Keep `value` if it is within [lo, hi]; otherwise fall back to `fallback`.
//
// Falling back to the setting's own default rather than to the nearest bound is
// deliberate for anything that names a control: a key index clamped to 255 is
// silently unbound, and the user has no way to tell that from a key that simply
// does not work. Returning the default leaves them a working game.
static int ConfigClamp (int value, int lo, int hi, int fallback)
{
    if (value < lo || value > hi)
	return fallback;

    return value;
}

#endif // CONFIG_BOUNDS_H
