// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//    Convert a DOOM MUS music lump into a Standard MIDI byte stream so a
//    modern synth (SDL2_mixer + FluidSynth) can play it. Algorithm ported
//    faithfully from Chocolate DOOM's mus2mid.c (Ben Ryves, Simon Howard);
//    the only change is the I/O: Chocolate DOOM's memio MEMFILE is replaced
//    with a small local growable byte buffer and a bounds-checked read over
//    the source lump, so this file is self-contained.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "mus2mid.h"

#define NUM_CHANNELS         16
#define MIDI_PERCUSSION_CHAN  9
#define MUS_PERCUSSION_CHAN  15

// MUS event codes (top nibble of an event descriptor).
typedef enum
{
    mus_releasekey       = 0x00,
    mus_presskey         = 0x10,
    mus_pitchwheel       = 0x20,
    mus_systemevent      = 0x30,
    mus_changecontroller = 0x40,
    mus_scoreend         = 0x60,
} musevent;

// MIDI event codes.
typedef enum
{
    midi_releasekey       = 0x80,
    midi_presskey         = 0x90,
    midi_changecontroller = 0xB0,
    midi_changepatch      = 0xC0,
    midi_pitchwheel       = 0xE0,
} midievent;

// Standard MIDI type-0 file header + single track header. The final four
// bytes (offset 18) are a placeholder for the track length, back-patched
// once the track size is known.
static const unsigned char midiheader[] =
{
    'M', 'T', 'h', 'd',     // Main header
    0x00, 0x00, 0x00, 0x06, // Header size
    0x00, 0x00,             // MIDI type (0)
    0x00, 0x01,             // Number of tracks
    0x00, 0x46,             // Resolution
    'M', 'T', 'r', 'k',     // Start of track
    0x00, 0x00, 0x00, 0x00  // Placeholder for track length
};

// MUS controller number -> MIDI controller number.
static const unsigned char controller_map[] =
{
    0x00, 0x20, 0x01, 0x07, 0x0A, 0x0B, 0x5B, 0x5D,
    0x40, 0x43, 0x78, 0x7B, 0x7E, 0x7F, 0x79
};

// All mutable conversion state, bundled so nothing is a file static.
typedef struct
{
    unsigned char *data;   // growable output buffer
    size_t         len;
    size_t         cap;
    int            error;  // set on OOM
    unsigned int   tracksize;
    unsigned int   queuedtime;
    unsigned char  channelvelocities[NUM_CHANNELS];
    int            channel_map[NUM_CHANNELS];
} musconv;

// Append n bytes to the growable output buffer. On OOM, sets error and
// becomes a no-op; callers check error after a batch of writes.
static void writebytes(musconv *c, const void *src, size_t n)
{
    if (c->error)
        return;

    if (c->len + n > c->cap)
    {
        size_t newcap = c->cap ? c->cap : 1024;
        unsigned char *p;

        while (newcap < c->len + n)
            newcap *= 2;

        p = (unsigned char *)realloc(c->data, newcap);
        if (!p)
        {
            c->error = 1;
            return;
        }
        c->data = p;
        c->cap = newcap;
    }

    memcpy(c->data + c->len, src, n);
    c->len += n;
}

static unsigned short rd_u16le(const unsigned char *p)
{
    return (unsigned short)(p[0] | (p[1] << 8));
}

// Write a variable-length MIDI timestamp.
static int write_time(musconv *c, unsigned int time)
{
    unsigned int buffer = time & 0x7F;
    unsigned char writeval;

    while ((time >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= ((time & 0x7F) | 0x80);
    }

    for (;;)
    {
        writeval = (unsigned char)(buffer & 0xFF);
        writebytes(c, &writeval, 1);
        if (c->error)
            return 1;
        c->tracksize++;

        if ((buffer & 0x80) != 0)
        {
            buffer >>= 8;
        }
        else
        {
            c->queuedtime = 0;
            return 0;
        }
    }
}

static int write_endtrack(musconv *c)
{
    unsigned char endtrack[] = { 0xFF, 0x2F, 0x00 };

    if (write_time(c, c->queuedtime))
        return 1;
    writebytes(c, endtrack, 3);
    if (c->error)
        return 1;
    c->tracksize += 3;
    return 0;
}

static int write_presskey(musconv *c, int channel, unsigned char key,
                          unsigned char velocity)
{
    unsigned char working;

    if (write_time(c, c->queuedtime))
        return 1;
    working = midi_presskey | channel;
    writebytes(c, &working, 1);
    working = key & 0x7F;
    writebytes(c, &working, 1);
    working = velocity & 0x7F;
    writebytes(c, &working, 1);
    if (c->error)
        return 1;
    c->tracksize += 3;
    return 0;
}

static int write_releasekey(musconv *c, int channel, unsigned char key)
{
    unsigned char working;

    if (write_time(c, c->queuedtime))
        return 1;
    working = midi_releasekey | channel;
    writebytes(c, &working, 1);
    working = key & 0x7F;
    writebytes(c, &working, 1);
    working = 0;
    writebytes(c, &working, 1);
    if (c->error)
        return 1;
    c->tracksize += 3;
    return 0;
}

static int write_pitchwheel(musconv *c, int channel, int wheel)
{
    unsigned char working;

    if (write_time(c, c->queuedtime))
        return 1;
    working = midi_pitchwheel | channel;
    writebytes(c, &working, 1);
    working = wheel & 0x7F;
    writebytes(c, &working, 1);
    working = (wheel >> 7) & 0x7F;
    writebytes(c, &working, 1);
    if (c->error)
        return 1;
    c->tracksize += 3;
    return 0;
}

static int write_changepatch(musconv *c, int channel, unsigned char patch)
{
    unsigned char working;

    if (write_time(c, c->queuedtime))
        return 1;
    working = midi_changepatch | channel;
    writebytes(c, &working, 1);
    working = patch & 0x7F;
    writebytes(c, &working, 1);
    if (c->error)
        return 1;
    c->tracksize += 2;
    return 0;
}

static int write_changecontroller_valued(musconv *c, int channel,
                                         unsigned char control,
                                         unsigned char value)
{
    unsigned char working;

    if (write_time(c, c->queuedtime))
        return 1;
    working = midi_changecontroller | channel;
    writebytes(c, &working, 1);
    working = control & 0x7F;
    writebytes(c, &working, 1);
    // Quirk preserved from vanilla DOOM / DMX: a value of 0x80 or more is
    // clamped to 0x7F. Kept so converted MIDI reproduces the original exactly.
    working = (value & 0x80) ? 0x7F : value;
    writebytes(c, &working, 1);
    if (c->error)
        return 1;
    c->tracksize += 3;
    return 0;
}

static int write_changecontroller_valueless(musconv *c, int channel,
                                            unsigned char control)
{
    return write_changecontroller_valued(c, channel, control, 0);
}

// Allocate the next free MIDI channel, skipping the percussion channel.
static int allocate_midichannel(musconv *c)
{
    int max = -1;
    int i;

    for (i = 0; i < NUM_CHANNELS; ++i)
        if (c->channel_map[i] > max)
            max = c->channel_map[i];

    if (max + 1 == MIDI_PERCUSSION_CHAN)
        return max + 2;
    return max + 1;
}

// Map a MUS channel to its MIDI channel, allocating on first use.
static int get_midichannel(musconv *c, int mus_channel)
{
    if (mus_channel == MUS_PERCUSSION_CHAN)
        return MIDI_PERCUSSION_CHAN;

    if (c->channel_map[mus_channel] == -1)
        c->channel_map[mus_channel] = allocate_midichannel(c);

    return c->channel_map[mus_channel];
}

int mus2mid(const unsigned char *mus, size_t muslen_real, unsigned char **mid_out, int *mid_len)
{
    musconv c;
    size_t muslen, pos;
    unsigned short scorelength, scorestart;
    int hitscoreend = 0;
    int i;

    *mid_out = NULL;
    *mid_len = 0;

    // DOOM-0093: need the full 8-byte header (magic + scorelength + scorestart)
    // present before reading any of it.
    if (muslen_real < 8)
        return 1;

    // Validate the MUS magic ("MUS\x1a").
    if (mus[0] != 'M' || mus[1] != 'U' || mus[2] != 'S' || mus[3] != 0x1a)
        return 1;

    scorelength = rd_u16le(mus + 4);
    scorestart  = rd_u16le(mus + 6);
    // Bound the score by the REAL lump size, not just the header's declared length
    // -- a crafted lump can inflate scorestart+scorelength past the buffer.
    muslen = (size_t)scorestart + scorelength;
    if (muslen > muslen_real)
        muslen = muslen_real;
    pos = scorestart;

    memset(&c, 0, sizeof c);
    for (i = 0; i < NUM_CHANNELS; ++i)
    {
        c.channel_map[i] = -1;
        c.channelvelocities[i] = 127;
    }

    // Bounds-checked single-byte read from the MUS lump.
#define READ_BYTE(dst)                       \
    do {                                     \
        if (pos >= muslen) goto fail;        \
        (dst) = mus[pos++];                  \
    } while (0)

    writebytes(&c, midiheader, sizeof midiheader);
    if (c.error)
        goto fail;

    while (!hitscoreend)
    {
        // Process a block of events sharing one time delay.
        while (!hitscoreend)
        {
            unsigned char eventdescriptor;
            unsigned char key, controllernumber, controllervalue;
            int channel;
            musevent event;

            READ_BYTE(eventdescriptor);
            channel = get_midichannel(&c, eventdescriptor & 0x0F);
            event = (musevent)(eventdescriptor & 0x70);

            switch (event)
            {
                case mus_releasekey:
                    READ_BYTE(key);
                    if (write_releasekey(&c, channel, key))
                        goto fail;
                    break;

                case mus_presskey:
                    READ_BYTE(key);
                    if (key & 0x80)
                    {
                        READ_BYTE(c.channelvelocities[channel]);
                        c.channelvelocities[channel] &= 0x7F;
                    }
                    if (write_presskey(&c, channel, key,
                                       c.channelvelocities[channel]))
                        goto fail;
                    break;

                case mus_pitchwheel:
                    READ_BYTE(key);
                    if (write_pitchwheel(&c, channel, key * 64))
                        goto fail;
                    break;

                case mus_systemevent:
                    READ_BYTE(controllernumber);
                    if (controllernumber < 10 || controllernumber > 14)
                        goto fail;
                    if (write_changecontroller_valueless(
                            &c, channel, controller_map[controllernumber]))
                        goto fail;
                    break;

                case mus_changecontroller:
                    READ_BYTE(controllernumber);
                    READ_BYTE(controllervalue);
                    if (controllernumber == 0)
                    {
                        if (write_changepatch(&c, channel, controllervalue))
                            goto fail;
                    }
                    else
                    {
                        if (controllernumber < 1 || controllernumber > 9)
                            goto fail;
                        if (write_changecontroller_valued(
                                &c, channel,
                                controller_map[controllernumber],
                                controllervalue))
                            goto fail;
                    }
                    break;

                case mus_scoreend:
                    hitscoreend = 1;
                    break;

                default:
                    goto fail;
            }

            if (eventdescriptor & 0x80)
                break;
        }

        // Read the time delay that follows the block.
        if (!hitscoreend)
        {
            unsigned int timedelay = 0;
            unsigned char working;

            for (;;)
            {
                READ_BYTE(working);
                timedelay = timedelay * 128 + (working & 0x7F);
                if ((working & 0x80) == 0)
                    break;
            }
            c.queuedtime += timedelay;
        }
    }

    if (write_endtrack(&c))
        goto fail;

    // Back-patch the track length into the placeholder at offset 18.
    c.data[18] = (c.tracksize >> 24) & 0xFF;
    c.data[19] = (c.tracksize >> 16) & 0xFF;
    c.data[20] = (c.tracksize >> 8) & 0xFF;
    c.data[21] = c.tracksize & 0xFF;

    *mid_out = c.data;
    *mid_len = (int)c.len;
    return 0;

#undef READ_BYTE

fail:
    free(c.data);
    return 1;
}

//-----------------------------------------------------------------------------
