// snd_handle_test.cpp — DOOM-0233: a sound handle names one use of a channel.
//
// Why this exists: I_StartSound returned the SDL_mixer channel number as the
// handle the game keeps. When a sound finished the mixer reused the channel, so an
// old handle then named a DIFFERENT sound: I_SoundIsPlaying(old) said yes, and
// I_UpdateSoundParams / I_StopSound re-panned or halted the wrong effect. A handle
// now carries a per-channel generation, so a handle from an earlier use is stale.
// The handle is treated as an opaque int throughout; only "non-negative" is assumed.
#include <climits>
#include <cstdio>
#include <cstring>

#include "../snd_handle.h"
#include "check_util.h"

int main()
{
    // ---- A zeroed table has issued nothing: no value resolves. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        const int probes[] = { 0, 1, 7, 255, 256, -1 };
        for (int p : probes)
            check_eq_int(SndHandleChannel(&t, p), -1,
                         "DOOM-0233: on a zeroed table a never-issued handle names nothing");
        int bad = 0;
        for (int v = 0; v <= 70000; v++)
            if (SndHandleChannel(&t, v) != -1) bad++;
        check_eq_int(bad, 0, "DOOM-0233: on a zeroed table no value 0..70000 resolves (count of values that did)");
    }

    // ---- One channel, opened twice: the first handle goes stale. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        const int h1 = SndHandleOpen(&t, 3);
        check(h1 >= 0, "DOOM-0233: a handle is non-negative (negative means no sound)");
        check_eq_int(SndHandleChannel(&t, h1), 3, "DOOM-0233: a fresh handle resolves to its channel");
        const int h2 = SndHandleOpen(&t, 3);
        check(h2 >= 0, "DOOM-0233: the second handle is non-negative");
        check(h2 != h1, "DOOM-0233: reopening a channel issues a different handle");
        check_eq_int(SndHandleChannel(&t, h2), 3, "DOOM-0233: the latest handle resolves to the channel");
        check_eq_int(SndHandleChannel(&t, h1), -1,
                     "DOOM-0233: the earlier handle for a reused channel is stale (names nothing)");
    }

    // ---- Different channels are independent. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        const int a = SndHandleOpen(&t, 0);
        const int b = SndHandleOpen(&t, 5);
        check_eq_int(SndHandleChannel(&t, a), 0, "DOOM-0233: channel 0's handle resolves to 0");
        check_eq_int(SndHandleChannel(&t, b), 5, "DOOM-0233: channel 5's handle resolves to 5");
        const int a2 = SndHandleOpen(&t, 0);
        check_eq_int(SndHandleChannel(&t, b), 5, "DOOM-0233: reopening channel 0 leaves channel 5's handle valid");
        check_eq_int(SndHandleChannel(&t, a2), 0, "DOOM-0233: channel 0's new handle resolves to 0");
        check_eq_int(SndHandleChannel(&t, a), -1, "DOOM-0233: channel 0's old handle is stale");
    }

    // ---- Many reuses: every handle stays non-negative and only the latest resolves. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        int prev = -1, neg = 0, wrongLatest = 0, prevLive = 0;
        for (int i = 0; i < 100000; i++)
        {
            const int h = SndHandleOpen(&t, 2);
            if (h < 0) neg++;
            if (SndHandleChannel(&t, h) != 2) wrongLatest++;
            if (prev >= 0 && prev != h && SndHandleChannel(&t, prev) != -1) prevLive++;
            prev = h;
        }
        check_eq_int(neg, 0, "DOOM-0233: over 100000 reuses no handle is negative (count that were)");
        check_eq_int(wrongLatest, 0, "DOOM-0233: over 100000 reuses the latest handle always resolves (count that did not)");
        check_eq_int(prevLive, 0, "DOOM-0233: over 100000 reuses the previous handle is always stale (count that resolved)");
    }

    // ---- Out-of-range channels. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        const int h = SndHandleOpen(&t, 4);
        snd_handles_t before = t;
        check_eq_int(SndHandleOpen(&t, -1), -1, "DOOM-0233: opening channel -1 fails");
        check_eq_int(SndHandleOpen(&t, SND_HANDLE_CHANNELS), -1, "DOOM-0233: opening the channel one past the table fails");
        check_eq_int(SndHandleOpen(&t, INT_MAX), -1, "DOOM-0233: opening a huge channel fails");
        check(std::memcmp(&before, &t, sizeof t) == 0, "DOOM-0233: a failed open leaves the table unchanged");
        check_eq_int(SndHandleChannel(&t, h), 4, "DOOM-0233: a previously issued handle still resolves after failed opens");
    }

    // ---- Wild handles. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        SndHandleOpen(&t, 1);
        check_eq_int(SndHandleChannel(&t, -1), -1, "DOOM-0233: handle -1 names nothing");
        check_eq_int(SndHandleChannel(&t, INT_MAX), -1, "DOOM-0233: handle INT_MAX names nothing");
        check_eq_int(SndHandleChannel(&t, INT_MIN), -1, "DOOM-0233: handle INT_MIN names nothing");
        // Whatever a wild value resolves to must be a real channel it is the latest handle of.
        int bad = 0;
        for (int v = 0; v <= 70000; v++)
        {
            const int c = SndHandleChannel(&t, v);
            if (c != -1 && (c < 0 || c >= SND_HANDLE_CHANNELS)) bad++;
        }
        check_eq_int(bad, 0, "DOOM-0233: no value resolves to a channel outside the table (count that did)");
    }

    // ---- The last channel works like any other. ----
    {
        snd_handles_t t;
        std::memset(&t, 0, sizeof t);
        const int last = SND_HANDLE_CHANNELS - 1;
        const int h1 = SndHandleOpen(&t, last);
        check(h1 >= 0, "DOOM-0233: the last channel yields a non-negative handle");
        check_eq_int(SndHandleChannel(&t, h1), last, "DOOM-0233: the last channel's handle resolves to it");
        const int h2 = SndHandleOpen(&t, last);
        check_eq_int(SndHandleChannel(&t, h2), last, "DOOM-0233: the last channel's new handle resolves to it");
        check_eq_int(SndHandleChannel(&t, h1), -1, "DOOM-0233: the last channel's old handle is stale");
    }

    return check_summary("snd_handle");
}
