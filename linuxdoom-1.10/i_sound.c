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

// DOOM-0047/0156: effects play through SDL2_mixer as chunks on the SAME device as
// the music (the Chocolate-Doom approach); the old hand-rolled software mixer has
// been removed. Each sound's raw 8-bit lump is kept cached, and S16-stereo chunks
// are built LAZILY per pitch bucket, so repeated sounds keep DOOM's subtle pitch
// variation (DOOM-0156) without pre-expanding every pitch of every sound. Playback
// is Mix_PlayChannel + Mix_SetPanning.
#define SFX_PITCH_BUCKETS	16	// bucket = pitch >> 4 (DOOM pitch 0..255, 128 = normal)

typedef struct
{
    boolean	loaded;				// source lump found + parsed
    int		lumpnum;			// 8-bit source lump (kept cached)
    int		rate;				// source sample rate (Hz)
    int		nsamp;				// source sample count
    Mix_Chunk*	pitched[SFX_PITCH_BUCKETS];	// lazily-built S16 chunks (NULL = not yet)
} sfxsound_t;

static sfxsound_t	sfx_snd[NUMSFX];
static int		mixer_freq = SAMPLERATE;	// device rate (from Mix_QuerySpec)
static boolean		sound_ok = false;	// the SDL2_mixer device opened OK





// (getsfx() removed -- DOOM-0047 replaced the software mixer with SDL2_mixer
//  chunks; see I_CacheSfx / I_BuildPitched below.)





// (addsfx() removed -- DOOM-0047 replaced the software mixer's channel table with
//  SDL2_mixer channels; see I_StartSound below.)





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


// DOOM-0047: cache a DOOM sound's raw 8-bit lump so S16 chunks can be built from it
// on demand (per pitch). The 8-byte lump header holds the sample rate at bytes 2-3
// and the sample count at 4-7. Non-fatal: a sound absent from this IWAD (e.g. a
// DOOM 2 effect under DOOM 1) is simply skipped -- it just won't play. Aliases
// (sfx->link) share their primary and are resolved at play time. The lump is kept
// cached (PU_STATIC) so I_BuildPitched can re-read it.
static boolean I_CacheSfx(int sfxid)
{
    char	namebuf[16];
    int		lumpnum;
    int		lumplen;
    byte*	lump;

    if (S_sfx[sfxid].link)
	return false;

    snprintf(namebuf, sizeof(namebuf), "ds%s", S_sfx[sfxid].name);
    lumpnum = W_CheckNumForName(namebuf);
    if (lumpnum < 0)
	return false;
    lumplen = W_LumpLength(lumpnum);
    if (lumplen <= 8)
	return false;

    lump = (byte*) W_CacheLumpNum(lumpnum, PU_STATIC);	// kept cached for pitch builds
    sfx_snd[sfxid].lumpnum = lumpnum;
    sfx_snd[sfxid].rate    = lump[2] | (lump[3] << 8);
    if (sfx_snd[sfxid].rate <= 0)
	sfx_snd[sfxid].rate = SFXRATE;
    sfx_snd[sfxid].nsamp = lump[4] | (lump[5] << 8) | (lump[6] << 16) | (lump[7] << 24);
    if (sfx_snd[sfxid].nsamp <= 0 || sfx_snd[sfxid].nsamp > lumplen - 8)
	sfx_snd[sfxid].nsamp = lumplen - 8;		// fall back to the raw byte count
    sfx_snd[sfxid].loaded = true;
    return true;
}


// DOOM-0156: build (once, then cached) the S16 stereo chunk for sound `id` at pitch
// `bucket` (representative DOOM pitch = bucket*16; 128 = normal). Resampling the
// 8-bit source as if it were at rate*pitch/128 shifts the playback pitch, restoring
// DOOM's subtle per-shot variation. Returns NULL on failure.
static Mix_Chunk* I_BuildPitched(int id, int bucket)
{
    byte*	lump;
    int		reppitch;
    int		claimrate;
    Mix_Chunk*	chunk;
    SDL_AudioCVT convertor;

    if (!sfx_snd[id].loaded)
	return NULL;
    if (sfx_snd[id].pitched[bucket])
	return sfx_snd[id].pitched[bucket];

    reppitch = bucket * 16;
    if (reppitch < 1)
	reppitch = 1;
    claimrate = (int)(((long)sfx_snd[id].rate * reppitch) / 128);	// 128 = NORM_PITCH
    if (claimrate < 1)
	claimrate = 1;

    if (SDL_BuildAudioCVT(&convertor, AUDIO_U8, 1, claimrate,
			  AUDIO_S16SYS, 2, mixer_freq) < 0)
	return NULL;

    lump = (byte*) W_CacheLumpNum(sfx_snd[id].lumpnum, PU_STATIC);
    convertor.len = sfx_snd[id].nsamp;
    convertor.buf = (Uint8*) malloc((size_t)convertor.len
				    * (convertor.len_mult > 0 ? convertor.len_mult : 1));
    if (!convertor.buf)
	return NULL;
    memcpy(convertor.buf, lump + 8, sfx_snd[id].nsamp);
    if (SDL_ConvertAudio(&convertor) < 0)
    {
	free(convertor.buf);
	return NULL;
    }

    chunk = (Mix_Chunk*) calloc(1, sizeof(Mix_Chunk));
    if (!chunk)
    {
	free(convertor.buf);
	return NULL;
    }
    chunk->allocated = 1;
    chunk->abuf = (Uint8*) malloc(convertor.len_cvt);
    if (!chunk->abuf)
    {
	free(chunk);
	free(convertor.buf);
	return NULL;
    }
    memcpy(chunk->abuf, convertor.buf, convertor.len_cvt);
    chunk->alen = convertor.len_cvt;
    chunk->volume = MIX_MAX_VOLUME;
    free(convertor.buf);

    sfx_snd[id].pitched[bucket] = chunk;
    return chunk;
}


// DOOM-0047: set a mixer channel's volume + stereo pan. vol is 0..15 (the
// snd_SfxVolume range); sep is 0..255 (128 = centre). Volume and pan are kept
// separate: Mix_Volume carries loudness (0..MIX_MAX_VOLUME) so a maxed effects
// slider plays at FULL level and can balance the music (which peaks ~48/128), and
// Mix_SetPanning carries only left/right balance -- full in both channels at centre
// (so centred sounds, incl. all menu blips, are at full volume), fading one side as
// the source moves off-centre. This is much louder than the classic ~12% software-
// mixer ceiling, but the slider is now meaningful across its whole range (the old
// mixer was near-silent by the time the level attenuated a centred sound). Higher
// sep = more to the right, matching the classic mixer's convention.
static void I_SetChanVolPan(int channel, int vol, int sep)
{
    int	mixvol, left, right;

    if (vol < 0)  vol = 0;  else if (vol > 15) vol = 15;
    mixvol = (vol * MIX_MAX_VOLUME) / 15;		// 0..128
    Mix_Volume(channel, mixvol);

    left  = 2 * (255 - sep);				// full (>=255) at/left of centre
    right = 2 * sep;					// full (>=255) at/right of centre
    if (left  < 0) left  = 0;  else if (left  > 255) left  = 255;
    if (right < 0) right = 0;  else if (right > 255) right = 255;
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
    // DOOM-0047/0156: play the sound's chunk for this pitch bucket on a free
    // SDL2_mixer channel and set its volume/pan. The channel index IS the handle
    // DOOM stores and later passes to I_UpdateSoundParams / I_StopSound /
    // I_SoundIsPlaying.
    {
      int channel, bucket;
      Mix_Chunk* chunk;

      if (!sound_ok || id < 0 || id >= NUMSFX)
	return -1;
      if (S_sfx[id].link)			// alias -> use the linked primary
	id = (int)(S_sfx[id].link - S_sfx);
      if (id < 0 || id >= NUMSFX || !sfx_snd[id].loaded)
	return -1;

      bucket = pitch >> 4;			// DOOM pitch 0..255 -> 0..15
      if (bucket < 1)			  bucket = 1;
      if (bucket >= SFX_PITCH_BUCKETS)	  bucket = SFX_PITCH_BUCKETS - 1;

      chunk = I_BuildPitched(id, bucket);
      if (!chunk)
	return -1;

      channel = Mix_PlayChannel(-1, chunk, 0);
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




// (I_MixSoundInto() and the SDL audio callbacks removed -- DOOM-0047 hands effects
//  to SDL2_mixer, which does the mixing on the shared device. See I_StartSound.)

// SDL2_mixer pulls and mixes audio on its own thread, so there is nothing to do
// per game-loop tick here.
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
#ifdef SNDINTR
  I_SoundDelTimer();
#endif

  // DOOM-0047/0156: effects play as SDL2_mixer chunks on the shared device -- stop
  // any playing channels and free every lazily-built pitch chunk. The device itself
  // is owned by the music side (Mix_CloseAudio runs in I_ShutdownMusic).
  if (sound_ok)
  {
    int s, b;
    Mix_HaltChannel(-1);
    for (s = 0; s < NUMSFX; s++)
      for (b = 0; b < SFX_PITCH_BUCKETS; b++)
	if (sfx_snd[s].pitched[b])
	{
	  free(sfx_snd[s].pitched[b]->abuf);
	  free(sfx_snd[s].pitched[b]);
	  sfx_snd[s].pitched[b] = NULL;
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

  // DOOM-0047: cache every sound's raw lump (I_CacheSfx); S16 chunks are built on
  // demand per pitch (I_BuildPitched) and played via Mix_PlayChannel (see
  // I_StartSound), on the same device as the music -- the fix for near-silent
  // effects on Windows.
  fprintf( stderr, "I_InitSound: ");
  if (sound_ok)
  {
    for (i=1 ; i<NUMSFX ; i++)
    {
      if (!S_sfx[i].link)
	I_CacheSfx(i);			// aliases resolve to their primary at play time
      // Non-NULL marker so s_sound.c doesn't warn "not pre-cached"; the chunk path
      // looks a sound up by id, not through this pointer.
      S_sfx[i].data = (void*) &sfx_snd[i];
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
  // DOOM-0165: log a start failure (a cold SDL_mixer/FluidSynth warm-up can fail
  // the very first play after launch); s_sound.c retries via I_QrySongPlaying.
  if (Mix_PlayMusic(s->music, looping ? -1 : 1) < 0)
    fprintf(stderr, "I_PlaySong: could not start music (%s)\n", Mix_GetError());
  // DOOM-0047: re-apply the music volume AFTER starting playback. SDL2_mixer's
  // MIDI backend (notably on Windows) begins a freshly-played track at full volume
  // and ignores a Mix_VolumeMusic set before playback started, so the saved/menu
  // level must be pushed again here. Otherwise music blared at max until the slider
  // was first touched -- which also drowned the effects ("SFX too soft on Windows").
  I_SetMusicVolume(snd_MusicVolume);
}

// DOOM-0165: report whether the given song is actually playing. Used by
// s_sound.c to confirm a music-start took hold; a cold FluidSynth warm-up can
// fail the first start after launch, in which case the caller retries.
int I_QrySongPlaying(int handle)
{
  song_t* s = song_for(handle);
  return (music_initialised && s) ? Mix_PlayingMusic() : 0;
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
