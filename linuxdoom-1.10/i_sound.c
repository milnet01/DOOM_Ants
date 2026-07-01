// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	System interface for sound.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: i_unix.c,v 1.5 1997/02/03 22:45:10 b1 Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <math.h>

#include <sys/time.h>
#include <sys/types.h>

#ifndef LINUX
#include <sys/filio.h>
#endif

#include <fcntl.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "mus2mid.h"

// Timer stuff. Experimental.
#include <time.h>
#include <signal.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"

// UNIX hack, to be removed.
#ifdef SNDSERV
// Separate sound server process.
FILE*	sndserver=0;
char*	sndserver_filename = "./sndserver ";
#elif SNDINTR

// Update all 30 millisecs, approx. 30fps synchronized.
// Linux resolution is allegedly 10 millisecs,
//  scale is microseconds.
#define SOUND_INTERVAL     500

// Get the interrupt. Set duration in millisecs.
int I_SoundSetTimer( int duration_of_tick );
void I_SoundDelTimer( void );
#else
// None?
#endif


// A quick hack to establish a protocol between
// synchronous mix buffer updates and asynchronous
// audio writes. Probably redundant with gametic.
#ifdef SNDINTR
static int flag = 0;            // only referenced by the SNDINTR interrupt path
#endif

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo)
//  mixing buffer, and the samplerate of the raw data.


// Needed for calling the actual sound output.
// 1024 frames @ 44100 Hz is a ~23 ms buffer -- low latency but enough margin to
// avoid WASAPI underruns on Windows at the higher output rate (DOOM-0047).
#define SAMPLECOUNT		1024
#define NUM_CHANNELS		8
// It is 2 for 16bit, and 2 for two channels.
#define BUFMUL                  4
#define MIXBUFFERSIZE		(SAMPLECOUNT*BUFMUL)

// DOOM-0047: the SFX mixer OUTPUT rate. Raised 11025 -> 44100 because SDL's Windows
// WASAPI backend resamples an 11025 Hz device badly (near-silent effects; SDL bug
// libsdl-org/SDL#1491). 44100 is the common native device rate (and matches the
// music device), so WASAPI does no awkward resampling. The DOOM sound lumps are
// SFXRATE (11025 Hz) sources; the step table below rescales them to this output
// rate so pitch is unchanged. Linux was already fine and stays bit-identical.
#define SAMPLERATE		44100	// Hz (SFX mixer output / device rate)
#define SFXRATE			11025	// Hz (DOOM sound-lump source rate)
#define SAMPLESIZE		2   	// 16bit

// The actual lengths of all sound effects.
int 		lengths[NUMSFX];

// The global mixing buffer.
// Basically, samples from all active internal channels
//  are modifed and added, and stored in the buffer
//  that is submitted to the audio device.
signed short	mixbuffer[MIXBUFFERSIZE];


// The channel step amount...
unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
unsigned int	channelstepremainder[NUM_CHANNELS];


// The channel data pointers, start and end.
unsigned char*	channels[NUM_CHANNELS];
unsigned char*	channelsend[NUM_CHANNELS];


// Time/gametic that the channel started playing,
//  used to determine oldest, which automatically
//  has lowest priority.
// In case number of active sounds exceeds
//  available channels.
int		channelstart[NUM_CHANNELS];

// The sound in channel handles,
//  determined on registration,
//  might be used to unregister/stop/modify,
//  currently unused.
int 		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
// Used to catch duplicates (like chainsaw).
int		channelids[NUM_CHANNELS];			

// Pitch to stepping lookup, unused.
int		steptable[256];

// DOOM-0047: effects now play through SDL2_mixer as chunks on the SAME device as
// the music (the Chocolate-Doom approach), replacing the hand-rolled software mixer
// that was near-silent on Windows (a second SDL audio device barely output). Each
// DOOM sound lump is converted once to the mixer's output format and cached here as
// a Mix_Chunk; playback is Mix_PlayChannel + Mix_SetPanning. The old software-mixer
// globals above (mixbuffer/channels/steptable/vol_lookup) and getsfx()/addsfx() are
// left dead for now (non-static, no warning); a later pass can drop them.
static Mix_Chunk	sfx_chunks[NUMSFX];	// converted sample, per sfx id
static boolean		sfx_chunk_ok[NUMSFX];	// true if that chunk built OK
static int		mixer_freq = SAMPLERATE;	// device rate (from Mix_QuerySpec)
static boolean		sound_ok = false;	// the SDL2_mixer device opened OK

// Volume lookups.
int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
int*		channelleftvol_lookup[NUM_CHANNELS];
int*		channelrightvol_lookup[NUM_CHANNELS];





//
// This function loads the sound data from the WAD lump,
//  for single sound.
//
void*
getsfx
( char*         sfxname,
  int*          len )
{
    unsigned char*      sfx;
    unsigned char*      paddedsfx;
    int                 i;
    int                 size;
    int                 paddedsize;
    char                name[20];
    int                 sfxlump;

    
    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    sprintf(name, "ds%s", sfxname);

    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.
    if ( W_CheckNumForName(name) == -1 )
      sfxlump = W_GetNumForName("dspistol");
    else
      sfxlump = W_GetNumForName(name);
    
    size = W_LumpLength( sfxlump );

    // Debug.
    // fprintf( stderr, "." );
    //fprintf( stderr, " -loading  %s (lump %d, %d bytes)\n",
    //	     sfxname, sfxlump, size );
    //fflush( stderr );
    
    sfx = (unsigned char*)W_CacheLumpNum( sfxlump, PU_STATIC );

    // Pads the sound effect out to the mixing buffer size.
    // The original realloc would interfere with zone memory.
    paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

    // Allocate from zone memory.
    paddedsfx = (unsigned char*)Z_Malloc( paddedsize+8, PU_STATIC, 0 );
    // ddt: (unsigned char *) realloc(sfx, paddedsize+8);
    // This should interfere with zone memory handling,
    //  which does not kick in in the soundserver.

    // Now copy and pad.
    memcpy(  paddedsfx, sfx, size );
    for (i=size ; i<paddedsize+8 ; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump.
    Z_Free( sfx );
    
    // Preserve padded length.
    *len = paddedsize;

    // Return allocated padded data.
    return (void *) (paddedsfx + 8);
}





//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int
addsfx
( int		sfxid,
  int		volume,
  int		step,
  int		seperation )
{
    static unsigned short	handlenums = 0;
 
    int		i;
    int		rc = -1;
    
    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup
	 || sfxid == sfx_sawidl
	 || sfxid == sfx_sawful
	 || sfxid == sfx_sawhit
	 || sfxid == sfx_stnmov
	 || sfxid == sfx_pistol	 )
    {
	// Loop all channels, check.
	for (i=0 ; i<NUM_CHANNELS ; i++)
	{
	    // Active, and using the same SFX?
	    if ( (channels[i])
		 && (channelids[i] == sfxid) )
	    {
		// Reset.
		channels[i] = 0;
		// We are sure that iff,
		//  there will only be one.
		break;
	    }
	}
    }

    // Loop all channels to find oldest SFX.
    for (i=0; (i<NUM_CHANNELS) && (channels[i]); i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    // Tales from the cryptic.
    // If we found a channel, fine.
    // If not, we simply overwrite the first one, 0.
    // Probably only happens at startup.
    if (i == NUM_CHANNELS)
	slot = oldestnum;
    else
	slot = i;

    // Okay, in the less recent channel,
    //  we will handle the new SFX.
    // Set pointer to raw data.
    channels[slot] = (unsigned char *) S_sfx[sfxid].data;
    // Set pointer to end of raw data.
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Reset current handle number, limited to 0..100.
    if (!handlenums)
	handlenums = 100;

    // Assign current handle number.
    // Preserved so sounds could be stopped (unused).
    channelhandles[slot] = rc = handlenums++;

    // Set stepping???
    // Kinda getting the impression this is never used.
    channelstep[slot] = step;
    // ???
    channelstepremainder[slot] = 0;
    // Should be gametic, I presume.
    channelstart[slot] = gametic;

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256
    seperation += 1;

    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    leftvol =
	volume - ((volume*seperation*seperation) >> 16); ///(256*256);
    seperation = seperation - 257;
    rightvol =
	volume - ((volume*seperation*seperation) >> 16);	

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");
    
    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");
    
    // Get the proper lookup table piece
    //  for this volume level???
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    channelids[slot] = sfxid;

    // You tell me.
    return rc;
}





//
// SFX API
// Note: this was called by S_Init.
// However, whatever they did in the
// old DPMS based DOS version, this
// were simply dummies in the Linux
// version.
// See soundserver initdata().
//
void I_SetChannels()
{
  // DOOM-0047: effects play as SDL2_mixer chunks now (see I_StartSound). Just make
  // sure the mixer has at least DOOM's mixing-slot count of channels; the device
  // was already opened in I_InitSound. (The old software-mixer lookup tables that
  // lived here are gone.)
  if (sound_ok)
    Mix_AllocateChannels(NUM_CHANNELS);
}

 
void I_SetSfxVolume(int volume)
{
  // Identical to DOS.
  // Basically, this should propagate
  //  the menu/config file setting
  //  to the state variable used in
  //  the mixing.
  snd_SfxVolume = volume;
}

//
// MUSIC API.
//
// Music plays on its OWN audio output via SDL2_mixer at 44100 Hz, fully
// independent of the 44100 Hz effects mixer above (which is untouched). MUS
// lumps are converted to MIDI in memory (mus2mid) and rendered by SDL2_mixer
// through FluidSynth + a General-MIDI soundfont. Music is non-essential: any
// failure logs a warning, disables music, and lets the game run with effects
// only. See docs/specs/DOOM-0016-music.md.
//

// Default General-MIDI soundfont (openSUSE path); overridable via
// $DOOM_SOUNDFONT for other distros and the future Windows build (DOOM-0006).
#define DEFAULT_SOUNDFONT "/usr/share/sounds/sf2/FluidR3_GM.sf2"

// DOOM only ever has one song registered at a time, but a tiny table keeps
// the handle->resources mapping explicit and the free path unambiguous.
#define MAX_SONGS 4

typedef struct
{
  boolean        in_use;
  Mix_Music*     music;
  unsigned char* midi;   // converted MIDI buffer, kept alive until unregister
} song_t;

static boolean music_initialised = false;
static song_t  songs[MAX_SONGS];

// Map a registered handle (1-based) back to its slot; NULL if out of range
// or not in use. Handle 0 means "no song" (e.g. music disabled).
static song_t* song_for(int handle)
{
  if (handle < 1 || handle > MAX_SONGS)
    return NULL;
  if (!songs[handle - 1].in_use)
    return NULL;
  return &songs[handle - 1];
}

void I_SetMusicVolume(int volume)
{
  // Internal state variable (DOOM's 0-15 scale).
  snd_MusicVolume = volume;

  if (music_initialised)
  {
    // Scale 0-15 -> SDL2_mixer's 0-128, but cap well below full: MIDI music on the
    // separate SDL2_mixer device is perceptibly louder than the software SFX mixer
    // at equal settings, so it would drown the effects (DOOM-0047). SFX can't be
    // boosted (a channel volume >127 is a hard I_Error in I_UpdateSoundParams), so
    // the rebalance lives on the music side. 48/128 (~38%) at max; tunable by ear.
    // (The SFX mixer now also outputs 44.1 kHz -- see SAMPLERATE -- which fixed the
    // near-silent Windows effects, but the MIDI-vs-PCM loudness gap remains.)
    int v = (volume * 48) / 15;
    if (v < 0)   v = 0;
    if (v > 128) v = 128;
    Mix_VolumeMusic(v);
  }
}


//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}


// DOOM-0047: convert one DOOM sound lump (8-bit unsigned PCM mono; the 8-byte
// header holds the sample rate at bytes 2-3 and the sample count at 4-7) into an
// S16 stereo Mix_Chunk at the mixer's output rate, cached in sfx_chunks[sfxid].
// Returns false on failure (that sound just won't play). Aliases (sfx->link) share
// the primary sound and are resolved at play time.
static boolean I_CacheSfxChunk(int sfxid)
{
    char	namebuf[16];
    int		lumpnum;
    int		lumplen;
    byte*	lump;
    int		rate;
    int		nsamp;
    SDL_AudioCVT convertor;

    if (S_sfx[sfxid].link)
	return false;

    // Non-fatal lookup: a sound absent from this IWAD (e.g. a DOOM 2 effect under
    // DOOM 1) is simply skipped -- it just won't play. (I_GetSfxLumpNum would
    // W_GetNumForName -> I_Error and abort startup.)
    snprintf(namebuf, sizeof(namebuf), "ds%s", S_sfx[sfxid].name);
    lumpnum = W_CheckNumForName(namebuf);
    if (lumpnum < 0)
	return false;
    lumplen = W_LumpLength(lumpnum);
    if (lumplen <= 8)
	return false;

    lump  = (byte*) W_CacheLumpNum(lumpnum, PU_STATIC);
    rate  = lump[2] | (lump[3] << 8);
    nsamp = lump[4] | (lump[5] << 8) | (lump[6] << 16) | (lump[7] << 24);
    if (rate <= 0)
	rate = SFXRATE;
    if (nsamp <= 0 || nsamp > lumplen - 8)
	nsamp = lumplen - 8;			// fall back to the raw byte count

    // Build U8 mono @ rate -> S16SYS stereo @ mixer_freq.
    if (SDL_BuildAudioCVT(&convertor, AUDIO_U8, 1, rate,
			  AUDIO_S16SYS, 2, mixer_freq) < 0)
    {
	Z_Free(lump);
	return false;
    }
    convertor.len = nsamp;
    convertor.buf = (Uint8*) malloc((size_t)convertor.len
				    * (convertor.len_mult > 0 ? convertor.len_mult : 1));
    if (!convertor.buf)
    {
	Z_Free(lump);
	return false;
    }
    memcpy(convertor.buf, lump + 8, nsamp);
    Z_Free(lump);
    if (SDL_ConvertAudio(&convertor) < 0)
    {
	free(convertor.buf);
	return false;
    }

    sfx_chunks[sfxid].allocated = 1;
    sfx_chunks[sfxid].abuf = (Uint8*) malloc(convertor.len_cvt);
    if (!sfx_chunks[sfxid].abuf)
    {
	free(convertor.buf);
	return false;
    }
    memcpy(sfx_chunks[sfxid].abuf, convertor.buf, convertor.len_cvt);
    sfx_chunks[sfxid].alen = convertor.len_cvt;
    sfx_chunks[sfxid].volume = MIX_MAX_VOLUME;
    free(convertor.buf);
    sfx_chunk_ok[sfxid] = true;
    return true;
}


// DOOM-0047: set a mixer channel's volume+pan to match the classic software mixer's
// per-channel loudness, so Linux is unchanged and Windows (now on the working
// SDL2_mixer device) matches it. vol is 0..15 (snd_SfxVolume range); sep is 0..255
// (128 = centre). Replicates the old addsfx() left/right split, then maps the ~0..15
// per-side level onto SDL_mixer's 0..255 panning (x2 == the classic ~12% ceiling).
static void I_SetChanVolPan(int channel, int vol, int sep)
{
    int	left, right;

    sep  += 1;
    left  = vol - ((vol * sep * sep) >> 16);
    sep   = sep - 257;
    right = vol - ((vol * sep * sep) >> 16);

    left  = (left  < 0 ? 0 : left)  * 2;
    right = (right < 0 ? 0 : right) * 2;
    if (left  > 255) left  = 255;
    if (right > 255) right = 255;

    Mix_SetPanning(channel, (Uint8)left, (Uint8)right);
}

//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
//
int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{

  // UNUSED
  priority = 0;
  
#ifdef SNDSERV 
    if (sndserver)
    {
	fprintf(sndserver, "p%2.2x%2.2x%2.2x%2.2x\n", id, pitch, vol, sep);
	fflush(sndserver);
    }
    // warning: control reaches end of non-void function.
    return id;
#else
    // DOOM-0047: play the pre-converted chunk on a free SDL2_mixer channel and set
    // its volume/pan. The channel index IS the handle DOOM stores and later passes
    // to I_UpdateSoundParams / I_StopSound / I_SoundIsPlaying. (Pitch variation from
    // the old software mixer is not applied to chunks -- a minor fidelity trade.)
    {
      int channel;

      if (!sound_ok || id < 0 || id >= NUMSFX)
	return -1;
      if (S_sfx[id].link)			// alias -> use the linked primary's chunk
	id = (int)(S_sfx[id].link - S_sfx);
      if (id < 0 || id >= NUMSFX || !sfx_chunk_ok[id])
	return -1;

      channel = Mix_PlayChannel(-1, &sfx_chunks[id], 0);
      if (channel < 0)
	return -1;				// no free channel

      I_SetChanVolPan(channel, vol, sep);
      return channel;
    }
#endif
}



void I_StopSound (int handle)
{
    if (sound_ok && handle >= 0)
	Mix_HaltChannel(handle);
}


int I_SoundIsPlaying(int handle)
{
    if (!sound_ok || handle < 0)
	return 0;
    return Mix_Playing(handle);
}




//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the global
//  mixbuffer, clamping it to the allowed range,
//  and sets up everything for transferring the
//  contents of the mixbuffer to the (two)
//  hardware channels (left and right, that is).
//
// This function currently supports only 16bit.
//
// DOOM-0047: mix all active effect channels INTO `out` (adding to whatever is
// already there -- music, in the SDL2_mixer post-mix path; silence in the
// standalone-device fallback), clamping to 16-bit. `frames` is stereo sample
// frames. Decoupled from SAMPLECOUNT so it fills whatever buffer the caller has.
static void I_MixSoundInto( signed short* out, int frames )
{
#ifdef SNDINTR
  // Debug. Count buffer misses with interrupt.
  static int misses = 0;
#endif

  
  // Mix current sound data.
  // Data, from raw sound, for right and left.
  register unsigned int	sample;
  register int		dl;
  register int		dr;
  
  // Pointers in global mixbuffer, left, right, end.
  signed short*		leftout;
  signed short*		rightout;
  signed short*		leftend;
  // Step in mixbuffer, left and right, thus two.
  int				step;

  // Mixing channel index.
  int				chan;
    
    // Left and right channel
    //  are in global mixbuffer, alternating.
    leftout = out;
    rightout = out+1;
    step = 2;

    // Determine end, for left channel only
    //  (right channel is implicit).
    leftend = out + frames*step;

    // Mix sounds into the mixing buffer.
    // Loop over step*SAMPLECOUNT,
    //  that is 512 values for two channels.
    while (leftout != leftend)
    {
	// Start from whatever is already in the buffer, so effects ADD to it
	// (music in the post-mix path; zero in the standalone fallback).
	dl = *leftout;
	dr = *rightout;

	// Love thy L2 chache - made this a loop.
	// Now more channels could be set at compile time
	//  as well. Thus loop those  channels.
	for ( chan = 0; chan < NUM_CHANNELS; chan++ )
	{
	    // Check channel, if active.
	    if (channels[ chan ])
	    {
		// Get the raw data from the channel. 
		sample = *channels[ chan ];
		// Add left and right part
		//  for this channel (sound)
		//  to the current data.
		// Adjust volume accordingly.
		dl += channelleftvol_lookup[ chan ][sample];
		dr += channelrightvol_lookup[ chan ][sample];
		// Increment index ???
		channelstepremainder[ chan ] += channelstep[ chan ];
		// MSB is next sample???
		channels[ chan ] += channelstepremainder[ chan ] >> 16;
		// Limit to LSB???
		channelstepremainder[ chan ] &= 65536-1;

		// Check whether we are done.
		if (channels[ chan ] >= channelsend[ chan ])
		    channels[ chan ] = 0;
	    }
	}
	
	// Clamp to range. Left hardware channel.
	// Has been char instead of short.
	// if (dl > 127) *leftout = 127;
	// else if (dl < -128) *leftout = -128;
	// else *leftout = dl;

	if (dl > 0x7fff)
	    *leftout = 0x7fff;
	else if (dl < -0x8000)
	    *leftout = -0x8000;
	else
	    *leftout = dl;

	// Same for right hardware channel.
	if (dr > 0x7fff)
	    *rightout = 0x7fff;
	else if (dr < -0x8000)
	    *rightout = -0x8000;
	else
	    *rightout = dr;

	// Increment current pointers in mixbuffer.
	leftout += step;
	rightout += step;
    }

#ifdef SNDINTR
    // Debug check.
    if ( flag )
    {
      misses += flag;
      flag = 0;
    }
    
    if ( misses > 10 )
    {
      fprintf( stderr, "I_SoundUpdate: missed 10 buffer writes\n");
      misses = 0;
    }
    
    // Increment flag for update.
    flag++;
#endif
}


// 
// This would be used to write out the mixbuffer
//  during each game loop update.
// Updates sound buffer and audio device at runtime. 
// It is called during Timer interrupt with SNDINTR.
// Mixing now done synchronous, and
//  only output be done asynchronous?
//
// DOOM-0047: preferred effects path. SDL2_mixer's post-mix hook hands us the FINAL
// music stream (the device's S16 stereo output); we add the effects straight into
// it, so effects and music share the ONE SDL2_mixer device. This fixes near-silent
// effects on Windows, where opening a SECOND (legacy SDL_OpenAudio) device for
// effects produced almost no output. len is bytes; a stereo S16 frame is 4 bytes.
static void __attribute__((unused)) I_SFXPostMix(void* udata, Uint8* stream, int len)
{
  (void)udata;
  I_MixSoundInto((signed short*)stream, len / (int)(2*sizeof(signed short)));
}

// Fallback effects path: a standalone SDL device, used only if the SDL2_mixer music
// device could not be opened (so effects still play without music). SDL hands us an
// uninitialised buffer, so zero it first, then mix effects in.
static void __attribute__((unused)) I_SDLAudioCallback(void* unused, Uint8* stream, int len)
{
  (void)unused;
  SDL_memset(stream, 0, len);
  I_MixSoundInto((signed short*)stream, len / (int)(2*sizeof(signed short)));
}

// The game loop still calls these every frame. Mixing is now pull-driven by
//  the callback above, so the synchronous update/submit are no-ops here.
void I_UpdateSound(void) {}

void
I_SubmitSound(void)
{
}



void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
  // DOOM-0047: re-pan/re-volume the playing chunk on this mixer channel as the
  // sound source moves relative to the player. (pitch is not applied to chunks.)
  (void)pitch;
  if (sound_ok && handle >= 0)
    I_SetChanVolPan(handle, vol, sep);
}




void I_ShutdownSound(void)
{    
#ifdef SNDSERV
  if (sndserver)
  {
    // Send a "quit" command.
    fprintf(sndserver, "q\n");
    fflush(sndserver);
  }
#else
  // Wait till all pending sounds are finished.
  int done = 0;
  int i;
  

  // FIXME (below).
  fprintf( stderr, "I_ShutdownSound: NOT finishing pending sounds\n");
  fflush( stderr );
  
  while ( !done )
  {
    for( i=0 ; i<8 && !channels[i] ; i++);
    
    // FIXME. No proper channel output.
    //if (i==8)
    done=1;
  }
#ifdef SNDINTR
  I_SoundDelTimer();
#endif
  
  // DOOM-0047: effects play as SDL2_mixer chunks on the shared device -- stop any
  // playing channels and free the converted chunk buffers. The device itself is
  // owned by the music side (Mix_CloseAudio runs in I_ShutdownMusic).
  if (sound_ok)
  {
    int s;
    Mix_HaltChannel(-1);
    for (s = 0; s < NUMSFX; s++)
      if (sfx_chunk_ok[s])
      {
	free(sfx_chunks[s].abuf);
	sfx_chunks[s].abuf = NULL;
	sfx_chunk_ok[s] = false;
      }
  }
#endif

  // Done.
  return;
}






void
I_InitSound()
{ 
#ifdef SNDSERV
  char buffer[256];
  
  if (getenv("DOOMWADDIR"))
    sprintf(buffer, "%s/%s",
	    getenv("DOOMWADDIR"),
	    sndserver_filename);
  else
    sprintf(buffer, "%s", sndserver_filename);
  
  // start sound process
  if ( !access(buffer, X_OK) )
  {
    strcat(buffer, " -quiet");
    sndserver = popen(buffer, "w");
  }
  else
    fprintf(stderr, "Could not start sound server [%s]\n", buffer);
#else
    
  int i;
  
#ifdef SNDINTR
  fprintf( stderr, "I_SoundSetTimer: %d microsecs\n", SOUND_INTERVAL );
  I_SoundSetTimer( SOUND_INTERVAL );
#endif
    
  // Bring up the SDL audio subsystem (SDL2_mixer needs it too).
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    I_Error("I_InitSound: SDL audio init failed: %s", SDL_GetError());

  // DOOM-0016/0047: open the ONE SDL2_mixer device (shared by effects AND music)
  // and set up music. I_InitMusic sets mixer_freq + sound_ok when the device opens,
  // and allocates the effect channels. (I_ShutdownMusic pairs on the shutdown side.)
  I_InitMusic();

  // DOOM-0047: convert every sound lump once to a Mix_Chunk at the device's output
  // rate (I_CacheSfxChunk). Effects then play via Mix_PlayChannel (see I_StartSound),
  // on the same device as the music -- the fix for near-silent effects on Windows.
  fprintf( stderr, "I_InitSound: ");
  if (sound_ok)
  {
    for (i=1 ; i<NUMSFX ; i++)
    {
      if (!S_sfx[i].link)
	I_CacheSfxChunk(i);		// aliases resolve to their primary at play time
      // Non-NULL marker so s_sound.c doesn't warn "not pre-cached"; the chunk path
      // looks a sound up by id, not through this pointer.
      S_sfx[i].data = (void*) &sfx_chunks[i];
    }
  }
  fprintf( stderr, " pre-cached all sound data\n");

  fprintf(stderr, "I_InitSound: sound module ready\n");

#endif
}




//
// MUSIC API.
//
// Real implementations (DOOM-0016). These are the platform side of the music
// contract that s_sound.c already calls; the shared state + helpers live up
// near I_SetMusicVolume. The stock dead "I_QrySongPlaying" dummy and its
// looping/musicdies statics are gone — their only caller was already commented
// out and they have no role in the SDL2_mixer path.
//

void I_InitMusic(void)
{
  const char*	soundfont;
  int		i;

  music_initialised = false;
  for (i = 0; i < MAX_SONGS; i++)
  {
    songs[i].in_use = false;
    songs[i].music = NULL;
    songs[i].midi = NULL;
  }

  // DOOM-0047: open the ONE audio device shared by effects (Mix_Chunks) AND music.
  // Effects need it even when MIDI is unavailable, so open it BEFORE the MIDI check.
  if (Mix_OpenAudioDevice(44100, MIX_DEFAULT_FORMAT, 2, 2048, NULL,
			  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE) < 0)
  {
    fprintf(stderr, "I_InitMusic: could not open audio device (%s); "
	    "sound disabled\n", Mix_GetError());
    return;			// sound_ok stays false -> no effects, no music
  }

  // Query the rate it actually opened at (ALLOW_FREQUENCY_CHANGE may pick 48 kHz);
  // effect chunks are converted to this rate. Allocate DOOM's effect channels.
  {
    int		freq = 44100, chans = 2;
    Uint16	fmt = MIX_DEFAULT_FORMAT;

    if (Mix_QuerySpec(&freq, &fmt, &chans) && freq > 0)
      mixer_freq = freq;
  }
  Mix_AllocateChannels(NUM_CHANNELS);
  sound_ok = true;		// the shared device is up: effects can play

  // MIDI music is optional. If the decoder is missing, keep effects and drop music.
  if ((Mix_Init(MIX_INIT_MID) & MIX_INIT_MID) == 0)
  {
    fprintf(stderr, "I_InitMusic: no MIDI support (%s); music disabled\n",
	    Mix_GetError());
    return;
  }

  // Soundfont: $DOOM_SOUNDFONT, then the system FluidR3_GM, else leave
  // SDL2_mixer to its built-in default backend (best-effort, not guaranteed).
  soundfont = getenv("DOOM_SOUNDFONT");
  if (soundfont && access(soundfont, R_OK) == 0)
    Mix_SetSoundFonts(soundfont);
  else if (access(DEFAULT_SOUNDFONT, R_OK) == 0)
    Mix_SetSoundFonts(DEFAULT_SOUNDFONT);

  music_initialised = true;

  // Apply the volume the menu/config already chose (0-15 -> 0-128).
  I_SetMusicVolume(snd_MusicVolume);

  fprintf(stderr, "I_InitMusic: music ready (44100 Hz, SDL2_mixer)\n");
}

void I_ShutdownMusic(void)
{
  int i;

  if (!music_initialised)
    return;

  Mix_HaltMusic();
  for (i = 0; i < MAX_SONGS; i++)
    if (songs[i].in_use)
      I_UnRegisterSong(i + 1);

  Mix_CloseAudio();
  Mix_Quit();
  music_initialised = false;
}

int I_RegisterSong(void* data)
{
  unsigned char*	midi;
  int			midilen;
  SDL_RWops*		rw;
  Mix_Music*		music;
  int			slot;

  if (!music_initialised)
    return 0;

  // Convert MUS -> MIDI. The MUS header carries its own length.
  if (mus2mid((const unsigned char*)data, &midi, &midilen) != 0)
  {
    fprintf(stderr, "I_RegisterSong: lump is not MUS music; skipping\n");
    return 0;
  }

  for (slot = 0; slot < MAX_SONGS; slot++)
    if (!songs[slot].in_use)
      break;
  if (slot == MAX_SONGS)
  {
    fprintf(stderr, "I_RegisterSong: song table full; skipping\n");
    free(midi);
    return 0;
  }

  // Keep the MIDI buffer alive until I_UnRegisterSong (defensive: do not
  // assume Mix_LoadMUS_RW copies it). freesrc frees only the RWops wrapper.
  rw = SDL_RWFromConstMem(midi, midilen);
  music = Mix_LoadMUS_RW(rw, SDL_TRUE);
  if (!music)
  {
    fprintf(stderr, "I_RegisterSong: %s\n", Mix_GetError());
    free(midi);
    return 0;
  }

  songs[slot].in_use = true;
  songs[slot].music = music;
  songs[slot].midi = midi;
  return slot + 1;
}

void I_PlaySong(int handle, int looping)
{
  song_t* s = song_for(handle);
  if (!s)
    return;
  // -1 loops forever; 1 plays the song once.
  Mix_PlayMusic(s->music, looping ? -1 : 1);
  // DOOM-0047: re-apply the music volume AFTER starting playback. SDL2_mixer's
  // MIDI backend (notably on Windows) begins a freshly-played track at full volume
  // and ignores a Mix_VolumeMusic set before playback started, so the saved/menu
  // level must be pushed again here. Otherwise music blared at max until the slider
  // was first touched -- which also drowned the effects ("SFX too soft on Windows").
  I_SetMusicVolume(snd_MusicVolume);
}

void I_PauseSong(int handle)
{
  if (music_initialised)
    Mix_PauseMusic();
}

void I_ResumeSong(int handle)
{
  if (music_initialised)
    Mix_ResumeMusic();
}

void I_StopSong(int handle)
{
  if (music_initialised)
    Mix_HaltMusic();
}

void I_UnRegisterSong(int handle)
{
  song_t* s = song_for(handle);
  if (!s)
    return;
  Mix_FreeMusic(s->music);
  free(s->midi);
  s->in_use = false;
  s->music = NULL;
  s->midi = NULL;
}



//
// Experimental stuff.
// A Linux timer interrupt, for asynchronous
//  sound output.
// I ripped this out of the Timer class in
//  our Difference Engine, including a few
//  SUN remains...
//
// Only used when SNDINTR drives output via /dev/dsp; the SDL backend pulls
// audio on its own thread, so this is left guarded as dead, original code.
#ifdef SNDINTR
#ifdef sun
    typedef     sigset_t        tSigSet;
#else    
    typedef     int             tSigSet;
#endif


// We might use SIGVTALRM and ITIMER_VIRTUAL, if the process
//  time independend timer happens to get lost due to heavy load.
// SIGALRM and ITIMER_REAL doesn't really work well.
// There are issues with profiling as well.
static int /*__itimer_which*/  itimer = ITIMER_REAL;

static int sig = SIGALRM;

// Interrupt handler.
void I_HandleSoundTimer( int ignore )
{
  // Debug.
  //fprintf( stderr, "%c", '+' ); fflush( stderr );
  
  // Feed sound device if necesary.
  if ( flag )
  {
    // See I_SubmitSound().
    // Write it to DSP device.
    write(audio_fd, mixbuffer, SAMPLECOUNT*BUFMUL);

    // Reset flag counter.
    flag = 0;
  }
  else
    return;
  
  // UNUSED, but required.
  ignore = 0;
  return;
}

// Get the interrupt. Set duration in millisecs.
int I_SoundSetTimer( int duration_of_tick )
{
  // Needed for gametick clockwork.
  struct itimerval    value;
  struct itimerval    ovalue;
  struct sigaction    act;
  struct sigaction    oact;

  int res;
  
  // This sets to SA_ONESHOT and SA_NOMASK, thus we can not use it.
  //     signal( _sig, handle_SIG_TICK );
  
  // Now we have to change this attribute for repeated calls.
  act.sa_handler = I_HandleSoundTimer;
#ifndef sun    
  //ac	t.sa_mask = _sig;
#endif
  act.sa_flags = SA_RESTART;
  
  sigaction( sig, &act, &oact );

  value.it_interval.tv_sec    = 0;
  value.it_interval.tv_usec   = duration_of_tick;
  value.it_value.tv_sec       = 0;
  value.it_value.tv_usec      = duration_of_tick;

  // Error is -1.
  res = setitimer( itimer, &value, &ovalue );

  // Debug.
  if ( res == -1 )
    fprintf( stderr, "I_SoundSetTimer: interrupt n.a.\n");
  
  return res;
}


// Remove the interrupt. Set duration to zero.
void I_SoundDelTimer()
{
  // Debug.
  if ( I_SoundSetTimer( 0 ) == -1)
    fprintf( stderr, "I_SoundDelTimer: failed to remove interrupt. Doh!\n");
}
#endif // SNDINTR
