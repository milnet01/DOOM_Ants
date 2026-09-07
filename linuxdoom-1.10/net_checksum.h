// net_checksum.h — DOOM-0256: the packet integrity check, turned back on.
//
// HGetPacket tests an incoming packet's checksum. That test did nothing at all:
// d_net.c carried
//
//     #ifdef NORMALUNIX
//         return 0;      // byte order problems
//     #endif
//
// and NORMALUNIX is defined by every build this project ships. So both peers
// computed 0, stored 0, and compared 0 against 0. Every packet passed.
//
// What it catches, now that it runs: a packet that arrived damaged, and a
// packet from a build whose doomdata_t is laid out differently -- the wire half
// of the problem DOOM-0426 describes for savegames. Without it, a mismatched
// peer's bytes are consumed as movement, buttons and consistancy.
//
// What it is NOT: a security control. The packet comes from an unauthenticated
// UDP peer, so anything hostile recomputes the sum along with the payload. What
// actually stops a malicious packet is the bounds work -- net_bounds.h
// (DOOM-0386), the numtics clamp (DOOM-0093) and the length check in
// HGetPacket.
//
// ---------------------------------------------------------------------------
// Do not read "byte-wise" as "endian-safe": the obvious reading is wrong here.
//
// Summing bytes rather than `unsigned` words removes the dependence on word
// layout, and covers the trailing bytes vanilla's /4 loop dropped. It does NOT
// make peers of different endianness agree, and nothing in this header could:
// the sum is over netbuffer, which is in HOST order at both ends, because
// i_net.c converts field by field on the way out and back. Two such peers hold
// the same values in different bytes.
//
// Acceptable because every platform this project ships is little-endian, and
// the alternative in force until now was no check at all. A big-endian peer
// would have its packets refused rather than misread. Making it genuinely
// cross-endian means checksumming a canonical wire form -- a protocol change,
// not a change to this function.
// ---------------------------------------------------------------------------
//
// Factored out here, rather than left inline in d_net.c, so tests can hold it
// against known byte sequences with no socket and no peer -- the same reason
// net_bounds.h and the other *_bounds.h headers exist.
#ifndef NET_CHECKSUM_H
#define NET_CHECKSUM_H

// The low 28 bits of doomdata_t.checksum. The top four carry NCMD_EXIT,
// NCMD_RETRANSMIT, NCMD_SETUP and NCMD_KILL, so the sum must not reach them.
#define NET_CHECKSUM_MASK	0x0fffffff

// Sum `length` bytes of `data`, weighting each by its position so that a
// transposition of two bytes changes the answer. The 0x1234567 seed is
// vanilla's, kept so the shape of the function is recognisably the original.
static unsigned NetPacketChecksum (const void* data, int length)
{
    const unsigned char*	p = (const unsigned char *) data;
    unsigned			c = 0x1234567;
    int				i;

    if (!p || length < 0)
	return c & NET_CHECKSUM_MASK;

    for (i = 0; i < length; i++)
	c += (unsigned) p[i] * (unsigned) (i + 1);

    return c & NET_CHECKSUM_MASK;
}

#endif // NET_CHECKSUM_H
