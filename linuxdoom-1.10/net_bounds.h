// net_bounds.h — DOOM-0386: how many tics a received datagram can actually hold.
//
// A game packet arrives from an unauthenticated UDP peer, so everything in it is
// untrusted (docs/standards/security.md). DOOM-0093 bounded the declared tic
// count against the cmds[] ARRAY, which stops the array overflow; it says nothing
// about whether the datagram carried that many commands. recvfrom's byte count
// lived in a local the copy loop then reused as its counter, so by the time
// anything could have compared them the count was gone.
//
// What that allowed: a 9-byte datagram declaring 12 tics passes the array check
// (12 <= BACKUPTICS) and the loop copies ~95 bytes of uninitialised stack into
// netbuffer->cmds, and from there into netcmds[][] as movement, buttons and
// consistancy. Not an out-of-bounds read -- the receive struct is fully
// allocated -- which is why a sanitizer does not see it and why the bound has to
// be written rather than discovered.
//
// Factored out here, rather than left inline in i_net.c, so tests can hold the
// boundary cases against it with no socket and no peer — the same reason
// save_bounds.h and wad_bounds.h exist.
#ifndef NET_BOUNDS_H
#define NET_BOUNDS_H

// Does a datagram of `received` bytes actually carry `numtics` commands, given a
// fixed header of `headersize` bytes and `cmdsize` bytes per command?
//
// The division is the point. Testing `headersize + numtics * cmdsize <= received`
// multiplies an attacker-controlled count and can wrap; dividing what is left
// after the header cannot.
static int NetPacketHoldsTics (int received, int numtics, int headersize, int cmdsize)
{
    if (headersize < 0 || cmdsize <= 0 || numtics < 0)
	return 0;
    if (received < headersize)
	return 0;
    return numtics <= (received - headersize) / cmdsize;
}

#endif
