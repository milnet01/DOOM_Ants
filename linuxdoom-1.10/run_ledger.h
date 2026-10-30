// run_ledger.h — DOOM-0502: one benchmark run as a frameledger run file.
//
// The format is frameledger's FLED-0001; frameledger.h formats the lines. This
// layer owns what the format leaves to the engine: which frame a row belongs to,
// when a frame's wall time is known, and getting the bytes onto disk.
//
// No DOOM headers and no clock: the caller passes the time in, so
// tests/run_ledger_test.cpp includes run_ledger.c as is.
#ifndef RUN_LEDGER_H
#define RUN_LEDGER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// The header's facts. Strings are copied and JSON-escaped by RL_Open.
typedef struct
{
    const char* run_id;
    const char* commit;
    int         dirty;
    const char* build_type;
    const char* device;
    const char* renderer;
    const char* driver;
    const char* api;
    int         width, height;
    int         render_scale_pct;
    int         vsync;
    int         pipelined;
    int         frames_in_flight;   // 0: unknown, left out
    const char* display;
    const char* scene;
    const char* tier;
    int         ray_tracing;
    double      gpu_tick_ns;        // 0: unknown, left out
    int         settling_frames;
    int         settled_frames;
    const char* os;
    const char* started_utc;
    const char* inputs_json;        // the whole array, already JSON
    double      load_start;         // below 0: unknown, left out
    double      gpu_busy_start;     // below 0: unknown, left out
} rl_facts_t;

// Starts a run writing to f, which must be seekable. cap is the row buffer's
// size; frames is how many to record before RL_FrameBegin says stop, 0 for no
// limit. Returns 1 on success.
int  RL_Open (FILE* f, const rl_facts_t* facts, size_t cap, long long frames);

// 1 from RL_Open until RL_Finish. The renderer's timers key off this.
int  RL_Active (void);

// A new frame starts at now_ms, which ends the one before it. Returns 1 when the
// frame limit is reached: call RL_Finish and stop.
int  RL_FrameBegin (double now_ms);

// The frame being drawn, for a GPU row that will be read back later; -1 when no
// run is open.
long long RL_CurrentFrame (void);

// This frame's segment ("level:e1m1"). A change writes event/cut on this frame,
// the first of the new segment.
void RL_SetSegment (const char* segment);

void RL_CpuPass (const char* name, double ms);
void RL_FenceWait (double ms);
void RL_PresentWait (double ms);
void RL_Event (const char* name, double count);

// A GPU result, read back after its frame ended. It counts against frame, the
// one that submitted the work. A frame before this run (below 0) is ignored.
void RL_GpuPass (long long frame, const char* name, int depth, double ms);
void RL_GpuSpan (long long frame, double ms);

// Ends the frame being drawn at now_ms, with no frame/wall, writes the close line
// and rewrites the header with the end-of-run readings. Does not close f. Below 0
// keeps a reading's slot at its opening value.
void RL_Finish (double now_ms, double load_end, double gpu_busy_end);

// sha256:<hex> of a file into out (at least 72 bytes), and its size. Returns 1
// on success.
int  RL_HashFile (const char* path, char* out, long long* bytes);

#ifdef __cplusplus
}
#endif

#endif
