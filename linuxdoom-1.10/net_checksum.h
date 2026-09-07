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
// On the byte order that got it switched off in 1997, because the obvious
// reading of "endian-safe" is wrong here and would cost the next reader a day:
//
// Summing BYTES instead of `unsigned` words removes the dependence on how a
// machine lays out a WORD, and it covers the trailing bytes vanilla's /4 loop
// dropped. It does NOT make two peers of DIFFERENT endianness agree, and
// nothing in this header could. The sum is taken over `netbuffer`, which is in
// HOST order at both ends: i_net.c's PacketSend and PacketGet convert
// field-by-field (htons on angleturn and consistancy) into a separate struct,
// so the two peers hold the same VALUES in different BYTES. Their sums differ.
//
// That is acceptable because every platform this project ships -- Linux and
// Windows on x86-64 -- is little-endian, and the alternative in force until now
// was no check whatever. Making it genuinely cross-endian means checksumming a
// canonical wire form rather than the host-order struct, which is a change to
// the protocol, not to this function.
//
// So: same-endian peers agree, which is all of them; a big-endian peer would
// have its packets rejected rather than silently misread, which is the safer of
// the two failures and better than what vanilla did.
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
