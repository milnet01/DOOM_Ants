// step_wall.h — DOOM-0500: where a step wall's far edge sits as planes move.
//
// A two-sided line has a lower step wall on each side, from that side's floor
// up to the other side's floor, and an upper step from the other side's ceiling
// up to its own. Classic re-derives which side shows one every frame. The 3D
// mesh is built once, so it builds the step on BOTH sides and lets the far edge
// follow the other sector's plane, clamped so it never crosses the near side's:
// a step that is not showing has zero height, and grows when a plane moves past
// its neighbour. Building only the side that showed at load left E1M8's star
// with no wall once the barons' death sank it, and the view ran under the next
// sector's floor to the sky.
//
// Factored out of r_mesh.c so tests/step_wall_test.cpp can hold the rule with
// no WAD, GPU or Vulkan link (mirrors sky_seal.h). Heights are fixed_t as int.
#ifndef STEP_WALL_H
#define STEP_WALL_H

// The top edge of a lower step: the other side's floor, never below this
// side's floor.
static int RB_LowerStepTop(int ownFloor, int otherFloor)
{
    return otherFloor > ownFloor ? otherFloor : ownFloor;
}

// The bottom edge of an upper step: the other side's ceiling, never above this
// side's ceiling.
static int RB_UpperStepBottom(int ownCeil, int otherCeil)
{
    return otherCeil < ownCeil ? otherCeil : ownCeil;
}

#endif
