// static_runs_test.cpp — DOOM-0612: which vertices the per-tic scan may check
// once per wall.
//
// Why this exists: RB_UpdateMeshHeights checks a run of still wall vertices
// once and writes its texture id to the whole run. That is only the old
// per-vertex answer if every vertex in the run reads the same sidedef slot,
// holds the same id to begin with, and never moves. A run that took in a
// moving vertex would leave its z frozen; a run spanning two walls would paint
// one wall's switch onto the other.
//
// Contract:
//   1  A run is consecutive still wall vertices (not flat, vtexside >= 0,
//      vplane NONE) with the same vtexside, vtexslot and texnum.
//   2  A moving vertex, a flat, or a vertex with no sidedef ends a run and is
//      in none.
//   3  Runs shorter than minlen are left out; the count pass (out NULL) and
//      the fill pass agree.
#include <cstdio>

#include "../static_runs.h"
#include "check_util.h"

namespace {

rb_vertex_t wallv(int side, int slot, int tex)
{
    rb_vertex_t v = {};
    v.vtexside = side;
    v.vtexslot = slot;
    v.texnum   = tex;
    v.vplane   = RB_PLANE_NONE;
    return v;
}

} // namespace

int main()
{
    // ---- 1 and 2: where runs start and stop --------------------------------
    {
        rb_vertex_t v[16];
        int k = 0;
        for (int i = 0; i < 4; i++) v[k++] = wallv(7, 1, 30);   // 0-3: one run
        v[k++] = wallv(8, 1, 30);                               // 4: other side
        v[k++] = wallv(8, 1, 30);                               // 5: same as 4
        v[k++] = wallv(8, 0, 30);                               // 6: other slot, alone
        rb_vertex_t moving = wallv(8, 0, 30);
        moving.vplane = RB_PLANE_FLOOR;
        v[k++] = moving;                                        // 7: moves
        v[k++] = wallv(9, 2, 40);                               // 8
        v[k++] = wallv(9, 2, 41);                               // 9: other texnum
        v[k++] = wallv(9, 2, 41);                               // 10
        rb_vertex_t flat = wallv(9, 2, 41);
        flat.flags = RB_MESH_FLAT;
        v[k++] = flat;                                          // 11: flat
        v[k++] = wallv(-1, 0, 41);                              // 12: no sidedef
        v[k++] = wallv(-1, 0, 41);                              // 13
        v[k++] = wallv(3, 1, 5);                                // 14-15: run at the end
        v[k++] = wallv(3, 1, 5);

        rb_vrun_t runs[16];
        const int n = RB_FindStaticRuns(v, k, 2, runs);
        check_eq_int(n, 4, "1: four runs of two or more");
        check_eq_int(runs[0].first, 0, "1: first run starts at 0");
        check_eq_int(runs[0].count, 4, "1: first run takes the four vertices of side 7");
        check_eq_int(runs[1].first, 4, "1: a new sidedef starts a new run");
        check_eq_int(runs[1].count, 2, "1: a new slot ends the run");
        check_eq_int(runs[2].first, 9, "1: a new texnum starts a new run");
        check_eq_int(runs[2].count, 2, "2: a flat ends the run and is in none");
        check_eq_int(runs[3].first, 14, "2: vertices with no sidedef are in no run");
        check_eq_int(runs[3].count, 2, "1: a run reaching the last vertex is kept");
        check(!RB_IsStaticWallVert(&moving), "2: a vertex on a moving plane is not still");
        check(!RB_IsStaticWallVert(&flat), "2: a flat is not a still wall vertex");
    }

    // ---- 3: minlen, and the two passes agree --------------------------------
    {
        rb_vertex_t v[6];
        for (int i = 0; i < 6; i++) v[i] = wallv(1, 1, 2);
        v[3] = wallv(2, 1, 2);                                  // runs of 3, 1, 2
        check_eq_int(RB_FindStaticRuns(v, 6, 2, NULL), 2, "3: count pass drops the run of one");
        check_eq_int(RB_FindStaticRuns(v, 6, 3, NULL), 1, "3: minlen 3 keeps only the run of three");
        rb_vrun_t runs[6];
        check_eq_int(RB_FindStaticRuns(v, 6, 1, runs), 3, "3: minlen 1 keeps every run");
        check_eq_int(runs[2].first, 4, "3: fill pass places the last run after the lone vertex");
        check_eq_int(RB_FindStaticRuns(v, 0, 2, NULL), 0, "3: an empty mesh has no runs");
    }

    return check_summary("static_runs_test");
}
