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
// DESCRIPTION:
//	DOOM graphics stuff, SDL2 backend.
//	Replaces the original 1997 X11/MITSHM layer (DOOM-0004). The engine
//	still renders into the 320x200 8-bit paletted screens[0]; here we map
//	that through the current palette into an ARGB texture and present it
//	with integer scaling.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

static SDL_Window*	window;
static SDL_Renderer*	renderer;
static SDL_Texture*	texture;

// 8-bit palette index -> ARGB8888, rebuilt by I_SetPalette.
static Uint32		palette[256];

// Integer scale of the 640x400 image (window is SCREENWIDTH*scale wide).
// Default 2 keeps the same physical window size as the old 320x200-at-scale-4.
static int		scale = 2;


// --- Gamepad (DOOM-0038) ----------------------------------------------------
//
// Wires SDL2's GameController API into DOOM's input. The classic ev_joystick
// path (G_Responder / M_Responder) already routes a four-button, two-axis pad
// through both gameplay and the menus -- including the menu's auto-repeat
// throttle -- so forward/back, turn, the four action buttons and menu
// navigation all reuse it unchanged. The two things the two-axis ev_joystick
// contract can't carry, an independent strafe axis and a menu-open button, are
// synthesised as the engine's existing keyboard events (the configured strafe
// keys and KEY_ESCAPE), so no engine code changes.
//
// Default layout (SDL / Xbox names; in-menu rebinding is future work):
//   Left stick      move forward/back (Y) and strafe (X)
//   Right stick X   turn
//   D-pad           move/turn, and menu navigation
//   A / Right Trig  fire            (menu: select)
//   B               strafe modifier (menu: back)
//   X / Y           use / open door
//   LB / RB / L Trig run (speed)
//   Start / Back    menu (Escape)
static SDL_GameController*	gamepad;

// The strafe keycodes live in g_game.c; gamepad strafe is posted through them
// so it honours any rebind and reuses the existing movement path.
extern int	key_straferight;
extern int	key_strafeleft;

#define GP_AXIS_DEADZONE	8000	// ~25% of the 32767 stick range
#define GP_TRIGGER_THRESH	16000	// half-pull on a 0..32767 trigger


//
// Translates an SDL key event into a DOOM key code.
//
static int xlatekey(SDL_Keysym* sym)
{
    int rc = sym->sym;

    switch (rc)
    {
      case SDLK_LEFT:	return KEY_LEFTARROW;
      case SDLK_RIGHT:	return KEY_RIGHTARROW;
      case SDLK_DOWN:	return KEY_DOWNARROW;
      case SDLK_UP:	return KEY_UPARROW;
      case SDLK_ESCAPE:	return KEY_ESCAPE;
      case SDLK_RETURN:	return KEY_ENTER;
      case SDLK_TAB:	return KEY_TAB;
      case SDLK_F1:	return KEY_F1;
      case SDLK_F2:	return KEY_F2;
      case SDLK_F3:	return KEY_F3;
      case SDLK_F4:	return KEY_F4;
      case SDLK_F5:	return KEY_F5;
      case SDLK_F6:	return KEY_F6;
      case SDLK_F7:	return KEY_F7;
      case SDLK_F8:	return KEY_F8;
      case SDLK_F9:	return KEY_F9;
      case SDLK_F10:	return KEY_F10;
      case SDLK_F11:	return KEY_F11;
      case SDLK_F12:	return KEY_F12;

      case SDLK_BACKSPACE:
      case SDLK_DELETE:	return KEY_BACKSPACE;

      case SDLK_PAUSE:	return KEY_PAUSE;

      case SDLK_KP_EQUALS:
      case SDLK_EQUALS:	return KEY_EQUALS;

      case SDLK_KP_MINUS:
      case SDLK_MINUS:	return KEY_MINUS;

      case SDLK_LSHIFT:
      case SDLK_RSHIFT:	return KEY_RSHIFT;

      case SDLK_LCTRL:
      case SDLK_RCTRL:	return KEY_RCTRL;

      case SDLK_LALT:
      case SDLK_RALT:	return KEY_RALT;

      default:
	// Printable ASCII (incl. lowercase letters) maps to itself.
	if (rc >= SDLK_SPACE && rc < 0x80)
	    return rc;
	return rc & 0xff;
    }
}


//
// Builds the DOOM mouse-button bitmask from an SDL button state.
//  bit0 = left, bit1 = middle, bit2 = right (matches the original X11 order).
//
static int mousebuttons(Uint32 state)
{
    return ((state & SDL_BUTTON_LMASK) ? 1 : 0)
	 | ((state & SDL_BUTTON_MMASK) ? 2 : 0)
	 | ((state & SDL_BUTTON_RMASK) ? 4 : 0);
}


//
// Gamepad open/close. Only the first game-controller is tracked.
//
static void I_OpenGamepad(int device_index)
{
    if (gamepad)
	return;
    if (SDL_IsGameController(device_index))
	gamepad = SDL_GameControllerOpen(device_index);
}

static void I_CloseGamepad(void)
{
    if (gamepad)
    {
	SDL_GameControllerClose(gamepad);
	gamepad = NULL;
    }
}


//
// Posts a synthetic key event only when the held state changes, so a held
// axis/button reads as one keydown..keyup pair (DOOM keys are edge-triggered).
//
static void I_PostKeyEdge(boolean down, boolean* held, int doomkey)
{
    event_t event;

    if (down == *held)
	return;
    *held = down;
    event.type = down ? ev_keydown : ev_keyup;
    event.data1 = doomkey;
    D_PostEvent(&event);
}


//
// Toggle the path-tracer view (DOOM-0009), shared by the `~` key and the gamepad
// touchpad (DOOM-0136). With the Debug Views menu toggle off (rb_rtdebug_menu==0)
// it is a plain ray-tracing on/off switch (denoised SVGF <-> raster); with it on
// it cycles the diagnostic views (mode 5, headless verify, is skipped). Flips the
// renderer flag directly; the Vulkan present path reads it, no-op without RT.
static void I_ToggleRtView(void)
{
    extern int rb_rtdebug;          // r_vulkan.cpp
    extern int rb_rtdebug_menu;     // r_vulkan.cpp (DOOM-0135)
    if (rb_rtdebug_menu)
    {
	rb_rtdebug++;
	if      (rb_rtdebug == 5) rb_rtdebug = 6;   // skip the verify-only mode
	else if (rb_rtdebug > 6)  rb_rtdebug = 0;
    }
    else
    {
	rb_rtdebug = (rb_rtdebug == 0) ? 6 : 0;     // plain RT on/off
    }
    {
	// Terminal proof of the active mode (the on-screen top-centre title is the
	// in-game proof; this mirrors it for headless/log debugging).
	static const char* const names[] = {
	    "Original (raster)", "HITS/normals", "white furnace",
	    "textured", "NEE direct (noisy)", "(verify)", "DENOISED (SVGF)"
	};
	printf("RT debug mode %d: %s\n", rb_rtdebug, names[rb_rtdebug]);
	fflush(stdout);
    }
}

// Reads the open gamepad once per tic and feeds DOOM's input: an ev_joystick
// for buttons + turn + forward (reused for menu navigation), plus synthetic
// strafe / Escape key edges for what the two-axis contract can't carry.
//
static void I_PollGamepad(void)
{
    event_t	event;
    int		lx, ly, rx, lt, rt;
    int		buttons = 0;
    boolean	b_pressed;
    extern boolean menuactive;          // m_menu.c: lets B (Circle) close the menu
    extern int rb_wireframe;            // r_vulkan.cpp: 3D wireframe debug toggle
    extern int rb_flashlight;           // r_vulkan.cpp: DOOM-0044 headlamp toggle
    static boolean strafe_r_held, strafe_l_held, esc_held, back_held, map_held;
    static boolean share_held, touchpad_held;
    static boolean dpad_up_held, dpad_l_held, dpad_r_held;	// DOOM-0153 gamepad remap

    if (!gamepad)
	return;

    lx = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX);
    ly = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY);
    rx = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTX);
    lt = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    rt = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    // data2 = turn (right stick X or d-pad L/R),
    // data3 = forward/back (left stick Y or d-pad U/D); SDL's Y is up-negative,
    // matching DOOM's forward == -1.
    event.type = ev_joystick;
    event.data2 = 0;
    event.data3 = 0;

    // Read the D-pad once. It drives menu navigation (turn/forward channels) ONLY
    // while a menu is open; in play it is the weapon/flashlight pad handled by the
    // edge block lower down (LEFT/RIGHT cycle weapons, UP toggles the flashlight),
    // so it no longer turns or walks (DOOM-0153 / user gamepad remap).
    boolean	dp_l = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    boolean	dp_r = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    boolean	dp_u = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    boolean	dp_d = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);

    if (rx > GP_AXIS_DEADZONE || (menuactive && dp_r))
	event.data2 = 1;
    else if (rx < -GP_AXIS_DEADZONE || (menuactive && dp_l))
	event.data2 = -1;

    if (ly < -GP_AXIS_DEADZONE || (menuactive && dp_u))
	event.data3 = -1;
    else if (ly > GP_AXIS_DEADZONE || (menuactive && dp_d))
	event.data3 = 1;

    // data1 button mask, matching the joyb* defaults: bit0 fire (menu select),
    // bit1 strafe modifier (menu back), bit2 speed/run, bit3 use.
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A)
	|| rt > GP_TRIGGER_THRESH)
	buttons |= 1;
    // B (Circle): the strafe / menu-back button in play, but while a menu is open
    // it acts as Escape (back/close) -- the PlayStation "back" convention -- so
    // only feed bit1 when no menu is up; the Escape edge below covers the menu.
    b_pressed = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B);
    if (b_pressed && !menuactive)
	buttons |= 2;
    // Run (speed): R1, L1, or either trigger. L1 is a run button again (DOOM-0153,
    // user mapping) -- the flashlight moved to D-pad UP in the edge block below.
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
	|| SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
	|| lt > GP_TRIGGER_THRESH)
	buttons |= 4;
    // Square (X) = use/open. Triangle (Y) is no longer a second use button; it
    // toggles the automap via the KEY_TAB edge posted below.
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X))
	buttons |= 8;
    event.data1 = buttons;

    D_PostEvent(&event);

    // Left stick X has no ev_joystick channel; drive the engine's strafe keys.
    I_PostKeyEdge(lx >  GP_AXIS_DEADZONE, &strafe_r_held, key_straferight);
    I_PostKeyEdge(lx < -GP_AXIS_DEADZONE, &strafe_l_held, key_strafeleft);

    // Start toggles the menu (open from play, close from anywhere) via Escape.
    I_PostKeyEdge(
	SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START),
	&esc_held, KEY_ESCAPE);

    // Share (PS4 "Back") toggles the 3D wireframe debug view on its press edge.
    // Not a DOOM key, so flip the renderer flag directly rather than post an
    // event; harmless in Classic (no Vulkan path reads it).
    {
	boolean share = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK);
	if (share && !share_held)
	    rb_wireframe = !rb_wireframe;
	share_held = share;
    }

    // DOOM-0153 / user gamepad remap: in play, the D-pad is the weapon + flashlight
    // pad. UP toggles the flashlight (DOOM-0044); RIGHT/LEFT cycle to the next/prev
    // owned weapon (fed into g_game.c's weaponcycle, consumed by G_BuildTiccmd like
    // a number key -- demo/net safe). Edge-detected so one press = one action, and
    // suppressed unless we're in actual gameplay (while a menu is up the D-pad
    // navigates it; in intermission/finale it does nothing).
    {
	extern int	weaponcycle;		// g_game.c
	boolean		live = (gamestate == GS_LEVEL) && !menuactive;
	boolean		up    = live && dp_u;
	boolean		left  = live && dp_l;
	boolean		right = live && dp_r;

	if (up && !dpad_up_held)
	    rb_flashlight = !rb_flashlight;
	if (right && !dpad_r_held)
	    weaponcycle += 1;			// next weapon
	if (left && !dpad_l_held)
	    weaponcycle -= 1;			// previous weapon

	dpad_up_held = up;
	dpad_r_held  = right;
	dpad_l_held  = left;
    }

    // DOOM-0136: clicking the RIGHT half of the touchpad toggles the path-tracer
    // view -- the gamepad equivalent of the `~` key. The whole-pad click is
    // BUTTON_TOUCHPAD; the finger position (SDL 2.0.14+) picks the half (x in
    // [0,1], so >= 0.5 is the right side). Edge-detected so one press = one toggle.
    // PS4/PS5 pads only; a controller with no touchpad never reports the button.
    {
	boolean tp = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_TOUCHPAD);
	if (tp && !touchpad_held)
	{
	    Uint8 fstate = 0; float fx = 0.0f, fy = 0.0f, fpres = 0.0f;
	    if (SDL_GameControllerGetTouchpadFinger(gamepad, 0, 0, &fstate, &fx, &fy, &fpres) == 0
		&& fstate && fx >= 0.5f)
		I_ToggleRtView();
	}
	touchpad_held = tp;
    }

    // Triangle (Y) toggles the automap. KEY_TAB is the engine's automap key
    // (AM_STARTKEY/AM_ENDKEY in am_map.c); posting it as an edge means one press
    // opens the map and the next closes it -- same open/close toggle as Escape.
    I_PostKeyEdge(
	SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y),
	&map_held, KEY_TAB);

    // B (Circle) is "back" while a menu is open: M_Responder backs out one level
    // and closes when there is none left. In play (no menu) B stays strafe (bit1).
    I_PostKeyEdge(b_pressed && menuactive, &back_held, KEY_BACKSPACE);
}


// DOOM-0161: name of the face button that confirms menu selections and yes/no
// prompts on the connected pad, for on-screen text (e.g. the Quit dialog). The
// engine's confirm is always SDL's BUTTON_A -- the bottom face button -- so we
// just label it the way the player's hardware does. SDL already classifies the
// controller family (SDL_GameControllerGetType), so no per-device database is
// needed. Returns NULL when no pad is connected; the label is uppercase to suit
// DOOM's uppercase-only menu font. Unknown pads fall back to "A": the vast
// majority of generic PC pads are XInput/Xbox-style with A on the bottom.
const char* I_ControllerConfirmLabel(void)
{
    if (!gamepad)
	return NULL;

    switch (SDL_GameControllerGetType(gamepad))
    {
      case SDL_CONTROLLER_TYPE_PS3:
      case SDL_CONTROLLER_TYPE_PS4:
      case SDL_CONTROLLER_TYPE_PS5:
	return "X";		// PlayStation: the X-shaped bottom button (gamers call it X, not "Cross")
      case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
	return "B";		// Nintendo: the bottom face button is labelled B
      default:
	return "A";		// Xbox + generic XInput default
    }
}


static void I_GetEvent(SDL_Event* sdlevent)
{
    event_t event;
    extern boolean menuactive;          // m_menu.c: F still types in menu text entry
    extern int rb_flashlight;           // r_vulkan.cpp: DOOM-0044 headlamp toggle

    switch (sdlevent->type)
    {
      case SDL_KEYDOWN:
	// F toggles the player flashlight (DOOM-0044) on its press edge, like the
	// `~` debug cycle below. Not a DOOM key, so flip the renderer flag directly.
	// Gated on !menuactive so F still types normally in save-name / menu entry.
	if (sdlevent->key.keysym.sym == SDLK_f && !sdlevent->key.repeat && !menuactive)
	{
	    rb_flashlight = !rb_flashlight;
	    break;
	}
	// Backquote/tilde (`~`) controls the path-tracer view (DOOM-0009). DOOM-0135:
	// with the "Debug Views" menu toggle OFF (the default, rb_rtdebug_menu==0) it
	// is a plain ray-tracing on/off switch (denoised SVGF <-> raster). With it ON
	// it cycles the full diagnostic set: off -> intersection -> white-furnace ->
	// textured (3a) -> NEE direct (3c, mode 4) -> SVGF denoised (step 6, mode 6);
	// mode 5 (headless -rtverify) is skipped. Not a DOOM key, so flip the renderer
	// flag directly instead of posting an event; the Vulkan present path reads it,
	// no-op without RT.
	if (sdlevent->key.keysym.sym == SDLK_BACKQUOTE && !sdlevent->key.repeat)
	{
	    I_ToggleRtView();           // DOOM-0135/0136: shared with the touchpad
	    break;
	}
	// Backslash (`\`) toggles the per-pass GPU profiler (DOOM-0090): prints the
	// path tracer's per-stage GPU cost (sprites/megakernel/denoise/blit) to the
	// terminal once a second. RT-only; no-op under Classic/Solid.
	if (sdlevent->key.keysym.sym == SDLK_BACKSLASH && !sdlevent->key.repeat)
	{
	    extern int rb_profile;          // r_vulkan.cpp
	    rb_profile = !rb_profile;
	    printf("Per-pass GPU profiler %s (Solid + Ultra)\n", rb_profile ? "ON" : "OFF");
	    fflush(stdout);
	    break;
	}
	// Right-bracket (`]`) cycles the Ultra RT de-tile quality (DOOM-0181): off ->
	// 2-tap -> 4-tap -> off. Lets the de-tiling be A/B'd live for the perf pass.
	// Ultra RT + HD-materials only; a no-op elsewhere (the populate site forces 0).
	if (sdlevent->key.keysym.sym == SDLK_RIGHTBRACKET && !sdlevent->key.repeat)
	{
	    extern int rb_detile;           // r_vulkan.cpp
	    static const char* det_name[3] = { "OFF", "2-tap", "4-tap" };
	    rb_detile = (rb_detile + 1) % 3;
	    printf("Ultra de-tile %s (Ultra RT only)\n", det_name[rb_detile]);
	    fflush(stdout);
	    break;
	}
	// Left-bracket (`[`) toggles the Ultra RT dirt-stain filth on/off (DOOM-0187).
	// Lets the stain layer be A/B'd live for the perf pass; also a standing perf option.
	// Ultra RT only; a no-op elsewhere.
	if (sdlevent->key.keysym.sym == SDLK_LEFTBRACKET && !sdlevent->key.repeat)
	{
	    extern int rb_filth;            // r_vulkan.cpp
	    rb_filth = !rb_filth;
	    printf("Ultra filth %s (Ultra RT only)\n", rb_filth ? "ON" : "OFF");
	    fflush(stdout);
	    break;
	}
	// Apostrophe (`'`) toggles the Ultra RT wet-liquid layers on/off (DOOM-0183):
	// the sheen, nukage ripples, and goo-puddle wet. The glow/cast-light is permanent
	// (CPU-built), so this is a sheen/ripple/puddle A/B, not a whole-feature switch.
	// Ultra RT only; a no-op elsewhere.
	if (sdlevent->key.keysym.sym == SDLK_QUOTE && !sdlevent->key.repeat)
	{
	    extern int rb_wet;              // r_vulkan.cpp
	    rb_wet = !rb_wet;
	    printf("Ultra wet-liquid %s (Ultra RT only)\n", rb_wet ? "ON" : "OFF");
	    fflush(stdout);
	    break;
	}
	// Semicolon (`;`) cycles the volumetric-fog strength (DOOM-0011): Off -> Low ->
	// Med -> High -> Off. Lets the fog be A/B'd live for the per-layer look + perf pass.
	// RT engaged only (modes 4/6, either tier); a no-op on the raster/Classic path.
	if (sdlevent->key.keysym.sym == SDLK_SEMICOLON && !sdlevent->key.repeat)
	{
	    extern int rb_fog;              // r_vulkan.cpp
	    static const char* fog_name[4] = { "OFF", "Low", "Med", "High" };
	    rb_fog = (rb_fog + 1) % 4;
	    printf("Volumetric fog: %s (RT only)\n", fog_name[rb_fog]);
	    fflush(stdout);
	    break;
	}
	event.type = ev_keydown;
	event.data1 = xlatekey(&sdlevent->key.keysym);
	D_PostEvent(&event);
	break;

      case SDL_KEYUP:
	event.type = ev_keyup;
	event.data1 = xlatekey(&sdlevent->key.keysym);
	D_PostEvent(&event);
	break;

      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
	event.type = ev_mouse;
	event.data1 = mousebuttons(SDL_GetMouseState(NULL, NULL));
	event.data2 = event.data3 = 0;
	D_PostEvent(&event);
	break;

      case SDL_MOUSEMOTION:
	event.type = ev_mouse;
	event.data1 = mousebuttons(sdlevent->motion.state);
	event.data2 = sdlevent->motion.xrel << 2;
	event.data3 = -sdlevent->motion.yrel << 2;
	if (event.data2 || event.data3)
	    D_PostEvent(&event);
	break;

      case SDL_MOUSEWHEEL:
	// DOOM-0153: scroll wheel cycles weapons. Post one notch per event; the
	// game (G_BuildTiccmd) steps to the next/previous owned weapon. y > 0 is
	// scroll-up = next weapon (flip handled by wheel.direction).
	{
	    int wy = sdlevent->wheel.y;
	    if (sdlevent->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
		wy = -wy;
	    if (wy != 0)
	    {
		event.type = ev_mousewheel;
		event.data1 = (wy > 0) ? 1 : -1;
		event.data2 = event.data3 = 0;
		D_PostEvent(&event);
	    }
	}
	break;

      case SDL_QUIT:
	I_Quit();
	break;

      case SDL_CONTROLLERDEVICEADDED:
	I_OpenGamepad(sdlevent->cdevice.which);
	break;

      case SDL_CONTROLLERDEVICEREMOVED:
	// cdevice.which is an instance id here; close only if it is ours.
	if (gamepad
	    && sdlevent->cdevice.which
	       == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad)))
	    I_CloseGamepad();
	break;

      default:
	break;
    }
}


//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}


//
// I_StartTic
//
void I_StartTic (void)
{
    SDL_Event sdlevent;

    if (!window)
	return;

    while (SDL_PollEvent(&sdlevent))
	I_GetEvent(&sdlevent);

    I_PollGamepad();
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}


//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
    static int	lasttic;
    int		tics;
    int		i;
    void*	pixels;
    int		pitch;
    int		x;
    int		y;
    byte*	src;

    // draws little dots on the bottom of the screen
    if (devparm)
    {
	i = I_GetTime();
	tics = i - lasttic;
	lasttic = i;
	if (tics > 20) tics = 20;

	for (i=0 ; i<tics*2 ; i+=2)
	    screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
	for ( ; i<20*2 ; i+=2)
	    screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
    }

    // Expand the 8-bit paletted frame into the ARGB streaming texture.
    SDL_LockTexture(texture, NULL, &pixels, &pitch);
    src = screens[0];
    for (y=0 ; y<SCREENHEIGHT ; y++)
    {
	Uint32* dst = (Uint32*)((Uint8*)pixels + y*pitch);
	for (x=0 ; x<SCREENWIDTH ; x++)
	    dst[x] = palette[src[y*SCREENWIDTH + x]];
    }
    SDL_UnlockTexture(texture);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}


//
// I_SetPalette
//
// Takes a full 8-bit 256*3 RGB palette, applies the current gamma and stores
// it as the ARGB lookup used by I_FinishUpdate.
//
void I_SetPalette (byte* pal)
{
    int		i;
    int		r;
    int		g;
    int		b;

    for (i=0 ; i<256 ; i++)
    {
	r = gammatable[usegamma][*pal++];
	g = gammatable[usegamma][*pal++];
	b = gammatable[usegamma][*pal++];
	palette[i] = (0xffu << 24) | (r << 16) | (g << 8) | b;
    }
}


void I_ShutdownGraphics(void)
{
    if (texture)	SDL_DestroyTexture(texture);
    if (renderer)	SDL_DestroyRenderer(renderer);
    if (window)		SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    I_CloseGamepad();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}


//
// I_GetWindow  (DOOM-0008)
//
// Hands the live SDL_Window to the Vulkan back-end as an opaque pointer, so the
// DOOM C translation units don't have to include SDL just to pass it through.
// The back-end casts it back to SDL_Window* for SDL_Vulkan_CreateSurface.
//
void* I_GetWindow(void)
{
    return window;
}


// Fullscreen is the default for the packaged builds; -windowed (-w) opts out.
// -fullscreen (-f) is still accepted (and harmless) for explicitness. Single
// source of truth so the Vulkan and Classic window paths can't drift (DOOM-0039).
static boolean I_WantFullscreen(void)
{
    return !(M_CheckParm("-windowed") || M_CheckParm("-w"));
}


//
// I_ShutdownGraphicsForVulkan  (DOOM-0008)
//
// The 3D back-end owns presentation, so it cannot share the 2D SDL_Renderer
// path. An existing window can't gain the Vulkan flag, so we tear down the
// renderer/texture/window and recreate the window with SDL_WINDOW_VULKAN. The
// SDL video subsystem stays initialised; only the 3D back-end is called after
// this, and only when it is the selected, available mode (DOOM-0026 clamp).
//
void I_ShutdownGraphicsForVulkan(void)
{
    Uint32 flags;

    if (texture)	SDL_DestroyTexture(texture);
    if (renderer)	SDL_DestroyRenderer(renderer);
    if (window)		SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;

    flags = SDL_WINDOW_VULKAN;
    if (I_WantFullscreen())
	flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
	flags |= SDL_WINDOW_RESIZABLE;

    window = SDL_CreateWindow(
	"DOOM",
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	SCREENWIDTH*scale, SCREENHEIGHT*scale,
	flags);
    if (!window)
	I_Error("I_ShutdownGraphicsForVulkan: could not create Vulkan window: %s",
		SDL_GetError());

    // Same input grab as the Classic window.
    SDL_SetRelativeMouseMode(SDL_TRUE);
}


//
// I_SetAspect  (DOOM-0147 Part C)
//
// Apply the present aspect from the `fillstretch` preference. Off (default):
// authentic DOOM pixel aspect -- logical SCREENWIDTH x SCREENHEIGHT*6/5, SDL
// letter-/pillar-boxes with black bars where the window shape differs. On: pass
// (0,0) to disable logical scaling, so the framebuffer stretches to fill the whole
// window (no bars, at the cost of dropping the 4:3 vertical correction). Safe to
// call any time -- the menu toggle re-applies it live; no-op before the renderer
// exists.
//
void I_SetAspect(void)
{
    if (!renderer)
	return;
    if (fillstretch)
	SDL_RenderSetLogicalSize(renderer, 0, 0);
    else
	SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENHEIGHT * 6 / 5);
}


//
// CreateSoftwareWindow  (DOOM-0008)
//
// Build the 2D window + accelerated renderer + streaming ARGB texture that the
// software path presents through. Shared by first-time init and the switch back
// from a 3D back-end (I_ReinitGraphicsForClassic). Assumes the SDL video
// subsystem is already up and `scale` is set.
//
static void CreateSoftwareWindow(void)
{
    Uint32 flags = I_WantFullscreen()
	? SDL_WINDOW_FULLSCREEN_DESKTOP
	: SDL_WINDOW_RESIZABLE;

    window = SDL_CreateWindow(
	"DOOM",
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	SCREENWIDTH*scale, SCREENHEIGHT*scale,
	flags);
    if (!window)
	I_Error("I_InitGraphics: could not create window: %s", SDL_GetError());

    renderer = SDL_CreateRenderer(window, -1,
	SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
	renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
	I_Error("I_InitGraphics: could not create renderer: %s", SDL_GetError());

    // Present with authentic DOOM pixel aspect (DOOM-0147). The framebuffer is
    // SCREENWIDTH x SCREENHEIGHT of square pixels, but DOOM's true display
    // stretched the 200 scanlines ~1.2x vertically. So the logical height is a
    // fixed SCREENHEIGHT*6/5 (= 400*1.2 = 480) regardless of width, and the
    // logical width is the runtime SCREENWIDTH. At 4:3 (SCREENWIDTH 640) this is
    // 640x480 -- identical to the old SCREENWIDTH*3/4 form. On a widescreen build
    // SCREENWIDTH is larger, so the logical area matches the display aspect and
    // fills the monitor edge-to-edge with MORE level shown (Hor+), no stretch.
    I_SetAspect();		// DOOM-0147 Part C: 4:3 present, or fill-stretch per pref

    texture = SDL_CreateTexture(renderer,
	SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
	SCREENWIDTH, SCREENHEIGHT);
    if (!texture)
	I_Error("I_InitGraphics: could not create texture: %s", SDL_GetError());

    // Trap and hide the cursor so relative motion drives turn/look,
    //  the way the original -grabmouse did.
    SDL_SetRelativeMouseMode(SDL_TRUE);
}


// DOOM-0147: the physical software-render width and the UI-centring offset.
// Defaults are vanilla 4:3 -- safe if I_InitWidescreen never runs or the display
// query fails. I_InitWidescreen (called before V_Init/R_Init) overrides them.
int		SCREENWIDTH = ORIGWIDTH * HIRES;
int		WIDESCREENDELTA = 0;

// DOOM-0147 Part C: persisted display prefs (loaded from ~/.doomrc before
// I_InitWidescreen). widescreen=0 forces 4:3 even on a wide display (honoured at
// startup, hence "restart to apply"); fillstretch=1 stretches the present to fill
// the monitor (applied live by I_SetAspect). Defaults reproduce Part B exactly.
int		widescreen = 1;
int		fillstretch = 0;

//
// I_InitWidescreen  (DOOM-0147)
//
// Pick the physical render width from the real display aspect so the classic
// renderer fills a widescreen monitor by showing MORE of the level (Hor+), not by
// stretching it. MUST run before V_Init/R_Init: both the screen buffers and the
// projection read SCREENWIDTH. A 4:3-or-narrower display (e.g. the 5:4 1280x1024)
// clamps to NONWIDEWIDTH and renders authentic 4:3 -- Hor+ has no width to add.
// SDL video is brought up here (idempotent; I_InitGraphics re-inits it later).
//
void I_InitWidescreen(void)
{
    SDL_DisplayMode	dm;
    double		aspect = 4.0 / 3.0;	// safe default if the query fails
    int			logical;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0
	&& SDL_GetDesktopDisplayMode(0, &dm) == 0
	&& dm.h > 0)
	aspect = (double)dm.w / (double)dm.h;

    // DOOM-0147: auto-detect. A 4:3-or-narrower display (or a failed query) has no
    // room for Hor+, so force the widescreen preference off there -- the menu then
    // honestly reads "Off" instead of an "On" that does nothing. Only a display
    // wider than 4:3 keeps the saved preference (default on).
    if (aspect <= 4.0 / 3.0 + 0.001)
	widescreen = 0;

    // active logical width = NONWIDEWIDTH * aspect / (4/3) = 240 * aspect, rounded.
    logical = (int)(240.0 * aspect + 0.5);
    if (!widescreen)			logical = NONWIDEWIDTH;		// Part C: user forced 4:3
    if (logical < NONWIDEWIDTH)		logical = NONWIDEWIDTH;		// narrower than 4:3
    if (logical > MAXWIDTH / HIRES)	logical = MAXWIDTH / HIRES;	// ultrawide cap (24:9)

    SCREENWIDTH = logical * HIRES;
    WIDESCREENDELTA = (logical - NONWIDEWIDTH) / 2;
}

void I_InitGraphics(void)
{
    int		pnum;
    static int	firsttime = 1;

    if (!firsttime)
	return;
    firsttime = 0;

    // Window scale: -2/-3 like the original, or -scale N.
    if (M_CheckParm("-2"))
	scale = 2;
    if (M_CheckParm("-3"))
	scale = 3;
    if ( (pnum = M_CheckParm("-scale")) && pnum < myargc-1 )
    {
	scale = atoi(myargv[pnum+1]);
	if (scale < 1) scale = 1;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
	I_Error("I_InitGraphics: SDL video init failed: %s", SDL_GetError());

    // Crisp nearest-neighbour upscale, no blur.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    // The 2D window + renderer + texture (shared with the 3D->Classic switch).
    CreateSoftwareWindow();

    // Gamepad support (DOOM-0038): optional. A box with no controller (or no
    // GameController support) just runs keyboard/mouse as before. SDL queues a
    // CONTROLLERDEVICEADDED for each already-connected pad, but open any here
    // too so the first frame already has input.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
    {
	int i;
	for (i = 0 ; i < SDL_NumJoysticks() ; i++)
	{
	    I_OpenGamepad(i);
	    if (gamepad)
		break;
	}
    }

    // screens[0] is allocated by V_Init; we only read it here.
}


//
// I_ReinitGraphicsForClassic  (DOOM-0008)
//
// Counterpart to I_ShutdownGraphicsForVulkan: switching a live game from a 3D
// back-end back to Classic. The window is then a SDL_WINDOW_VULKAN window with
// the 2D SDL_Renderer torn down (renderer == NULL), so destroy it and rebuild
// the software window/renderer/texture. No-op on the normal Classic path, where
// the 2D renderer is already live (first-boot and Classic->Classic).
//
void I_ReinitGraphicsForClassic(void)
{
    if (renderer)
	return;
    if (window)
    {
	SDL_DestroyWindow(window);
	window = NULL;
    }
    CreateSoftwareWindow();
}
