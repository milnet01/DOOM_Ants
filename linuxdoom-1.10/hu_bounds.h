// hu_bounds.h — DOOM-0250: copying a chat line into the HUD's message buffer.
//
// HU_Responder keeps the last chat line in a fixed buffer of
// HU_MAXLINELENGTH+1 bytes and copied into it with strcpy. One of the two
// sources is a chat macro, which comes from ~/.doomrc -- untrusted input
// (docs/standards/security.md) -- and M_LoadDefaults accepts a longer string
// than that buffer holds, so a hand-edited macro wrote past it.
//
// The copy lives here, rather than inline in hu_stuff.c, so
// tests/hu_bounds_test.cpp can hold the boundary cases against it with no
// engine and no globals -- the same reason the other *_bounds.h headers exist.
#ifndef HU_BOUNDS_H
#define HU_BOUNDS_H

#include <stddef.h>
#include <string.h>

// Copy `src` into the `cap`-byte buffer `dst`, truncating to fit and always
// terminating. A NULL source reads as empty. With cap == 0 nothing is written.
static void HU_CopyMessage (char* dst, size_t cap, const char* src)
{
    size_t	len;

    if (!cap)
	return;

    len = src ? strlen (src) : 0;
    if (len > cap - 1)
	len = cap - 1;

    // len is 0 for a NULL source, and memcpy's pointers must not be NULL even
    // for a zero-length copy.
    if (len)
	memcpy (dst, src, len);
    dst[len] = 0;
}

#endif // HU_BOUNDS_H
