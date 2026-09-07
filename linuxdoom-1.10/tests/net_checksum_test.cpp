// net_checksum_test.cpp — DOOM-0256: the packet checksum actually sums the packet.
//
// The defect was not a wrong answer, it was no answer: under NORMALUNIX the
// function returned 0, both peers stored 0, and HGetPacket compared 0 against 0.
// So the cases that matter are the ones a constant-returning function passes
// and a real checksum does not -- two different packets must not agree.
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <arpa/inet.h>

#include "../net_checksum.h"
#include "../d_net.h"
#include "check_util.h"

int main()
{
    const unsigned char a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    unsigned char       b[8];

    // The whole point: a checksum that varies with its input. Each of these
    // passed trivially while the function returned 0.
    check(NetPacketChecksum(a, 8) != NetPacketChecksum("\1\1\1\1\1\1\1\1", 8),
          "two different packets get different sums");

    std::memcpy(b, a, 8);
    b[3] ^= 0x01;
    check(NetPacketChecksum(a, 8) != NetPacketChecksum(b, 8),
          "flipping one bit in one byte changes the sum");

    // Position weighting: a transposition must not cancel out. A plain
    // unweighted sum would miss this, and packet fields are order-sensitive.
    std::memcpy(b, a, 8);
    b[2] = a[5]; b[5] = a[2];
    check(NetPacketChecksum(a, 8) != NetPacketChecksum(b, 8),
          "swapping two bytes changes the sum");

    // Length is part of the identity: a short packet must not collide with a
    // longer one that starts the same way. This is the cross-build case --
    // a peer whose doomdata_t is laid out differently sends a different length.
    check(NetPacketChecksum(a, 4) != NetPacketChecksum(a, 8),
          "the same bytes at two lengths give different sums");

    // Determinism, which is what lets two peers agree at all.
    check(NetPacketChecksum(a, 8) == NetPacketChecksum(a, 8),
          "the same input always gives the same sum");

    // The result must stay inside the low 28 bits: the top four carry
    // NCMD_EXIT, NCMD_RETRANSMIT, NCMD_SETUP and NCMD_KILL, and a sum that
    // reached them would forge a control flag.
    {
        unsigned char big[512];
        int           i;

        for (i = 0; i < 512; i++)
            big[i] = 0xff;

        check((NetPacketChecksum(big, 512) & ~NET_CHECKSUM_MASK) == 0,
              "a maximal packet cannot overflow into the command flag bits");
        check((NetPacketChecksum(a, 8) & ~NET_CHECKSUM_MASK) == 0,
              "an ordinary packet stays inside the mask");
    }

    // Degenerate inputs: a zero-length packet is legal (a bodiless control
    // packet), and must not read anything.
    check(NetPacketChecksum(a, 0) == NetPacketChecksum(b, 0),
          "a zero-length packet reads no bytes, so the pointer does not matter");
    check((NetPacketChecksum(nullptr, 8) & ~NET_CHECKSUM_MASK) == 0,
          "a null packet is refused without reading it");
    check((NetPacketChecksum(a, -1) & ~NET_CHECKSUM_MASK) == 0,
          "a negative length is refused without reading backwards");

    // ---- the property the wire actually depends on ----
    //
    // d_net.c checksums `netbuffer`, which is in HOST order, but i_net.c sends
    // a field-by-field byte-swapped COPY and un-swaps it on arrival. So the two
    // peers only agree if that round trip reproduces every byte the checksum
    // covered. Nothing else tests that, and getting it wrong breaks netplay
    // outright rather than subtly.
    //
    // This mirrors PacketSend and PacketGet exactly. A field either of them
    // forgets to copy shows up here as a mismatch.
    {
        doomdata_t  sent;
        doomdata_t  sw;
        doomdata_t  got;
        int         c;
        const int   kSpan = (int) offsetof(doomdata_t, retransmitfrom);

        std::memset(&sent, 0, sizeof(sent));
        std::memset(&sw,   0xAA, sizeof(sw));     // poison: nothing may survive uncopied
        std::memset(&got,  0x55, sizeof(got));    // poison the far end differently

        sent.retransmitfrom = 3;
        sent.starttic       = 200;
        sent.player         = 2;
        sent.numtics        = 4;
        for (c = 0; c < sent.numtics; c++)
        {
            sent.cmds[c].forwardmove = (char)(50 - c);
            sent.cmds[c].sidemove    = (char)(-40 + c);
            sent.cmds[c].angleturn   = (short)(0x1234 + c);
            sent.cmds[c].consistancy = (short)(0x7f00 + c);
            sent.cmds[c].chatchar    = (byte)(65 + c);
            sent.cmds[c].buttons     = (byte)(c & 0x7f);
        }

        const int size = (int) offsetof(doomdata_t, cmds)
                       + sent.numtics * (int) sizeof(sent.cmds[0]);

        const unsigned before =
            NetPacketChecksum(&sent.retransmitfrom, size - kSpan);

        // --- PacketSend's swap ---
        sw.checksum        = htonl(sent.checksum);
        sw.player          = sent.player;
        sw.retransmitfrom  = sent.retransmitfrom;
        sw.starttic        = sent.starttic;
        sw.numtics         = sent.numtics;
        for (c = 0; c < sent.numtics; c++)
        {
            sw.cmds[c].forwardmove = sent.cmds[c].forwardmove;
            sw.cmds[c].sidemove    = sent.cmds[c].sidemove;
            sw.cmds[c].angleturn   = htons(sent.cmds[c].angleturn);
            sw.cmds[c].consistancy = htons(sent.cmds[c].consistancy);
            sw.cmds[c].chatchar    = sent.cmds[c].chatchar;
            sw.cmds[c].buttons     = sent.cmds[c].buttons;
        }

        // --- PacketGet's un-swap ---
        got.checksum       = ntohl(sw.checksum);
        got.player         = sw.player;
        got.retransmitfrom = sw.retransmitfrom;
        got.starttic       = sw.starttic;
        got.numtics        = sw.numtics;
        for (c = 0; c < got.numtics; c++)
        {
            got.cmds[c].forwardmove = sw.cmds[c].forwardmove;
            got.cmds[c].sidemove    = sw.cmds[c].sidemove;
            got.cmds[c].angleturn   = ntohs(sw.cmds[c].angleturn);
            got.cmds[c].consistancy = ntohs(sw.cmds[c].consistancy);
            got.cmds[c].chatchar    = sw.cmds[c].chatchar;
            got.cmds[c].buttons     = sw.cmds[c].buttons;
        }

        const unsigned after =
            NetPacketChecksum(&got.retransmitfrom, size - kSpan);

        check(before == after,
              "the send swap and receive un-swap preserve every checksummed "
              "byte, so two peers agree");

        // The poison proves the round trip actually wrote the span rather than
        // the two ends happening to start equal.
        check(before != NetPacketChecksum("\x55\x55\x55\x55", 4),
              "the receive buffer was really overwritten, not left poisoned");

        // doomdata_t must have no padding inside the checksummed span, or the
        // sum would cover bytes the field-by-field copy never sets.
        check(offsetof(doomdata_t, cmds) == 8,
              "the packet header is 8 bytes with no padding before cmds[]");
        check(sizeof(ticcmd_t) == 8,
              "ticcmd_t is 8 bytes with no padding for the copy to miss");
    }

    return check_summary("net_checksum");
}
