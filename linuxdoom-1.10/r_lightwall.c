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
//    DOOM-0652 B2: the linedefs that block the rasterised view's lights (r_lightwall.h).
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "doomdata.h"   // ML_TWOSIDED
#include "m_fixed.h"
#include "r_defs.h"
#include "r_state.h"    // lines, numlines
#include "r_lightwall.h"

// lgrid's wall kinds (light_grid.h, C++ only).
enum { LW_OPEN = 0, LW_ONESIDED = 1, LW_CLOSED = 2 };

int RB_LightWalls(rb_lwall_t* out, int max)
{
    int i;
    if (!out)
        return numlines;
    if (max < numlines)
        return 0;
    for (i = 0; i < numlines; i++)
    {
        const line_t* ld = &lines[i];
        rb_lwall_t*   w  = &out[i];
        w->x1 = (float)ld->v1->x / FRACUNIT;  w->y1 = (float)ld->v1->y / FRACUNIT;
        w->x2 = (float)ld->v2->x / FRACUNIT;  w->y2 = (float)ld->v2->y / FRACUNIT;
        if (!(ld->flags & ML_TWOSIDED) || !ld->backsector)
            w->kind = LW_ONESIDED;
        else if (ld->frontsector == ld->backsector)
            w->kind = LW_OPEN;       // a self-referencing line is a see-through map trick
        else
        {
            // Shut when no gap is left between the lower ceiling and the higher floor, as a
            // closed door is: the same openrange <= 0 test P_LineOpening makes.
            const sector_t* f = ld->frontsector;
            const sector_t* b = ld->backsector;
            const fixed_t top = f->ceilingheight < b->ceilingheight ? f->ceilingheight : b->ceilingheight;
            const fixed_t bot = f->floorheight   > b->floorheight   ? f->floorheight   : b->floorheight;
            w->kind = top <= bot ? LW_CLOSED : LW_OPEN;
        }
    }
    return numlines;
}
