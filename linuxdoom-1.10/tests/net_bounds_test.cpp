// net_bounds_test.cpp — DOOM-0386: a datagram may not claim more tics than it carries.
//
// DOOM-0093 bounded the declared tic count against the cmds[] array, which stops
// the array overflow. Nothing compared it with the bytes actually received:
// recvfrom's count sat in a local that the copy loop then reused as its counter,
// so it was gone before anything could have. A 9-byte packet declaring 12 tics
// passes the array check and copies ~95 bytes of uninitialised stack into
// netcmds[][] as movement, buttons and consistancy, from an unauthenticated peer.
//
// That is not an out-of-bounds read -- the receive struct is fully allocated --
// so no sanitizer finds it, and i_net.c cannot be unit tested (it wants a socket
// and a peer). The bound therefore lives in net_bounds.h and is tested here, the
// way save_bounds.h and wad_bounds.h are.
#include <cstdio>
#include <climits>

#include "../net_bounds.h"
#include "check_util.h"

int main()
{
    // The real shapes: an 8-byte header and 8 bytes per ticcmd. Named here rather
    // than included, so a change to either is a deliberate edit to this test.
    const int kHdr = 8;
    const int kCmd = 8;

    // --- Packets that carry what they claim. ---
    check(NetPacketHoldsTics(kHdr, 0, kHdr, kCmd) != 0,
          "a header-only packet may claim no tics");
    check(NetPacketHoldsTics(kHdr + kCmd, 1, kHdr, kCmd) != 0,
          "a packet with room for one tic may claim one");
    check(NetPacketHoldsTics(kHdr + 12 * kCmd, 12, kHdr, kCmd) != 0,
          "a full packet may claim every tic it carries");
    check(NetPacketHoldsTics(kHdr + 12 * kCmd + 3, 12, kHdr, kCmd) != 0,
          "trailing bytes beyond the last tic do not invalidate the packet");

    // --- Packets that do not. ---
    // The roadmap's case: 9 bytes on the wire, 12 tics declared. It passes the
    // BACKUPTICS array check, which is why the array check alone was not enough.
    check(NetPacketHoldsTics(9, 12, kHdr, kCmd) == 0,
          "a 9-byte packet declaring 12 tics is refused");
    check(NetPacketHoldsTics(kHdr + kCmd, 2, kHdr, kCmd) == 0,
          "a packet claiming one more tic than it carries is refused");
    check(NetPacketHoldsTics(kHdr - 1, 0, kHdr, kCmd) == 0,
          "a packet shorter than the header is refused even claiming no tics");
    check(NetPacketHoldsTics(0, 0, kHdr, kCmd) == 0, "an empty datagram is refused");
    check(NetPacketHoldsTics(-1, 0, kHdr, kCmd) == 0,
          "a negative received count is refused");
    check(NetPacketHoldsTics(kHdr, -1, kHdr, kCmd) == 0, "a negative tic count is refused");

    // --- The overflow a naive `hdr + numtics * cmd <= received` check gets wrong. ---
    // Multiplying the attacker's count wraps; dividing what is left cannot.
    check(NetPacketHoldsTics(kHdr, INT_MAX, kHdr, kCmd) == 0,
          "a tic count near INT_MAX is refused, not wrapped into acceptance");
    check(NetPacketHoldsTics(INT_MAX, INT_MAX, kHdr, kCmd) == 0,
          "even the largest datagram cannot hold INT_MAX tics");

    // --- Degenerate shapes. ---
    check(NetPacketHoldsTics(64, 1, kHdr, 0) == 0, "a zero command size is refused");
    check(NetPacketHoldsTics(64, 1, -1, kCmd) == 0, "a negative header size is refused");

    // --- DOOM-0401: a doomcom's population against the arrays it indexes. ---
    // The two array sizes differ, which is the whole defect: MAXNETNODES is 8
    // and MAXPLAYERS is 4. Named here rather than included, so a change to
    // either is a deliberate edit to this test.
    const int kNodes = 8;
    const int kPlayers = 4;

    check(NetPopulationFits(1, 1, kNodes, kPlayers) != 0,
          "a single-player doomcom fits");
    check(NetPopulationFits(4, 4, kNodes, kPlayers) != 0,
          "a full four-player game fits");

    // The roadmap's case: `-net 1 h1..h5` gives numnodes = numplayers = 6, which
    // is inside MAXNETNODES and past the end of playeringame[].
    check(NetPopulationFits(6, 6, kNodes, kPlayers) == 0,
          "six nodes claiming six players is refused, not accepted as 6 <= 8");
    check(NetPopulationFits(5, 5, kNodes, kPlayers) == 0,
          "the first population past MAXPLAYERS is refused");
    check(NetPopulationFits(8, 8, kNodes, kPlayers) == 0,
          "a full MAXNETNODES host list is refused as a player count");

    // A driver may legitimately run more nodes than players; the node count is
    // bounded by its own array, not by MAXPLAYERS.
    check(NetPopulationFits(8, 4, kNodes, kPlayers) != 0,
          "eight nodes carrying four players fits both arrays");
    check(NetPopulationFits(9, 4, kNodes, kPlayers) == 0,
          "one node past MAXNETNODES is refused");

    check(NetPopulationFits(0, 0, kNodes, kPlayers) == 0,
          "an empty doomcom is refused");
    check(NetPopulationFits(1, 0, kNodes, kPlayers) == 0,
          "a game with no players is refused");
    check(NetPopulationFits(-1, 1, kNodes, kPlayers) == 0,
          "a negative node count is refused");
    check(NetPopulationFits(1, -1, kNodes, kPlayers) == 0,
          "a negative player count is refused");
    check(NetPopulationFits(INT_MAX, INT_MAX, kNodes, kPlayers) == 0,
          "counts near INT_MAX are refused");

    return check_summary("net_bounds_test");
}
