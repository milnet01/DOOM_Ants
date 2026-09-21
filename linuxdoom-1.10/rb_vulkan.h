// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2026 DOOM_Ants contributors
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
//    The Vulkan back-end's entry points (DOOM-0405) -- one declaration each,
//    seen by the C++ side that defines them and the C side that calls them.
//
//    Before this header the C side hand-wrote its own `extern` for every one of
//    them, so the two sides were independent copies of the same signature with
//    nothing comparing them. Both are plain C linkage, so a signature that
//    changed on one side and not the other LINKED, and the mismatch surfaced at
//    run time as a corrupt argument rather than at build time as an error. That
//    is the whole reason this file exists; there is no other.
//
//    Why it is not r_backend.h. That header pulls d_player.h and the rest of the
//    DOOM C headers, which are not C++-clean, so r_vulkan.cpp cannot include it
//    -- which is exactly how the copies came about. This header includes nothing
//    but r_mesh.h, which is pure C POD by its own description and is already
//    included by both sides. Keep it that way: anything added here that drags in
//    a DOOM header puts r_vulkan.cpp back on its own hand-written copies.
//
//    Two symbols r_vulkan.cpp exports are deliberately NOT here, because a
//    header cannot check them. `rendermode` and `gamestate` are mirrored inside
//    r_vulkan.cpp as plain ints with the reasoning written at the mirror site;
//    gamestate is an enum whose width this relies on. They are the same class of
//    unchecked copy and they need the type, not a declaration, so they are
//    untouched here.
//
//-----------------------------------------------------------------------------

#ifndef __RB_VULKAN__
#define __RB_VULKAN__

#include "r_mesh.h"     // rb_view_t -- the POD camera handed across the seam

#ifdef __cplusplus
extern "C" {
#endif

// Headless capability probe: the best tier this machine supports, as an RB_*
// value. Needs no window or surface, so RB_Init calls it before anything is
// created. Declared here rather than in r_backend.h so the definition in
// r_vulkan.cpp is checked against it.
int  RB_VulkanProbe(void);

// want_rt: require the hardware ray-tracing extensions. Nonzero if this machine
// can run the back-end on those terms.
int  RB_Vulkan_Available(int want_rt);

void RB_Vulkan_Init(void);
void RB_Vulkan_SetResolution(int w, int h);
void RB_Vulkan_RenderView(const rb_view_t* view);
void RB_Vulkan_SetOverlay(const unsigned char* pixels, int w, int h);
void RB_Vulkan_Present(void);
void RB_Vulkan_Shutdown(void);
void RB_Vulkan_BuildLevel(void);

#ifdef __cplusplus
}
#endif

#endif
