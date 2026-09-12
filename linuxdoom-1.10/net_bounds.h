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
// `static inline`, not plain `static`: two translation units include this header
// and each uses only one of the two predicates, so plain `static` warns about the
// other under -Wall.
static inline int NetPacketHoldsTics (int received, int numtics, int headersize, int cmdsize)
{
    if (headersize < 0 || cmdsize <= 0 || numtics < 0)
	return 0;
    if (received < headersize)
	return 0;
    return numtics <= (received - headersize) / cmdsize;
}

// DOOM-0401: does a doomcom's declared population fit the arrays the engine
// indexes with those counts? The two counts index arrays of DIFFERENT sizes:
// numnodes indexes nodeingame[MAXNETNODES], numplayers indexes
// playeringame[MAXPLAYERS], and MAXNETNODES is twice MAXPLAYERS.
//
// What that allowed: I_InitNetwork sets numplayers = numnodes and bounds only
// numnodes, so `-net 1 h1 h2 h3 h4 h5` reached D_CheckNetGame with numplayers
// of 6. Its fill loop then wrote playeringame[4] and [5], which in d_net.c's
// translation unit is the memory after a MAXPLAYERS-sized boolean array -- a
// write past the end of the array, from a plain command line.
//
// DOOM is a four-player game: players[], playeringame[], netcmds[] and the
// demo format are all MAXPLAYERS. So a larger population is refused rather
// than truncated -- dropping the extra hosts silently would start a game the
// operator did not ask for.
static inline int NetPopulationFits (int numnodes, int numplayers,
			      int maxnetnodes, int maxplayers)
{
    if (numnodes < 1 || numplayers < 1)
	return 0;
    if (numnodes > maxnetnodes)
	return 0;
    return numplayers <= maxplayers;
}

#endif
