// snd_handle.h — DOOM-0233: the handle I_StartSound gives the game.
//
// SDL2_mixer plays each effect on a numbered channel and reuses the number as
// soon as the effect ends. The game keeps the handle it was given and later
// asks "is it still playing?", re-pans it as the source moves, or stops it.
// When the handle WAS the channel number, a handle kept past its sound's end
// named whatever the mixer had put on that channel since: the game re-panned
// or halted somebody else's sound.
//
// So a handle carries the channel and a per-channel generation. Starting a
// sound on a channel bumps its generation; a handle resolves only while its
// generation is still the channel's current one.
//
// Pure, so tests/snd_handle_test.cpp holds it without an audio device.
#ifndef SND_HANDLE_H
#define SND_HANDLE_H

// More than SDL2_mixer is ever asked for; the channel lives in the low byte.
#define SND_HANDLE_CHANNELS	64

// Generations run 1..SND_HANDLE_GENMAX and wrap to 1. Zero means "no handle
// issued", so a zeroed table resolves nothing. Kept to 22 bits so the handle
// (generation << 8 | channel) stays a non-negative int.
#define SND_HANDLE_GENMAX	0x3fffffu

typedef struct
{
    unsigned	gen[SND_HANDLE_CHANNELS];
} snd_handles_t;

// A new handle for a sound just started on `channel`, or -1 when the channel
// is outside the table. Every earlier handle for that channel goes stale.
static inline int SndHandleOpen (snd_handles_t* t, int channel)
{
    unsigned	g;

    if (channel < 0 || channel >= SND_HANDLE_CHANNELS)
	return -1;

    g = t->gen[channel];
    g = (g >= SND_HANDLE_GENMAX) ? 1u : g + 1u;
    t->gen[channel] = g;

    return (int)((g << 8) | (unsigned)channel);
}

// The mixer channel `handle` names, or -1 when it is stale or was never
// issued.
static inline int SndHandleChannel (const snd_handles_t* t, int handle)
{
    int		channel;
    unsigned	g;

    if (handle < 0)
	return -1;

    channel = handle & 0xff;
    g = (unsigned)handle >> 8;

    if (channel >= SND_HANDLE_CHANNELS || g == 0)
	return -1;

    return t->gen[channel] == g ? channel : -1;
}

#endif // SND_HANDLE_H
