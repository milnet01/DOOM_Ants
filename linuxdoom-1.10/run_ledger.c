// run_ledger.c — DOOM-0502: see run_ledger.h.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "frameledger.h"
#include "run_ledger.h"

#define RL_HEADER_MAX	8192
#define RL_SEGMENT_MAX	(2 * FLED_PART_MAX + 2)

static struct
{
    FILE*	file;
    char*	buf;
    size_t	cap;
    fled_writer	writer;

    rl_facts_t	facts;
    char	header[RL_HEADER_MAX];

    long long	frame;		// the frame being drawn; -1 before the first
    long long	limit;
    double	start_ms;
    double	fence_ms;
    double	present_ms;
    int		active;

    // A GPU result arrives while the next frame is being drawn, and a row carries
    // its frame's segment, so the frame before keeps its name until then.
    char	segment[RL_SEGMENT_MAX];
    char	prev_segment[RL_SEGMENT_MAX];
} rl;

// Decimal text with three places, by hand: the format forbids the locale's
// decimal point, and "%f" would use it.
static void RL_Fixed3 (char* out, size_t n, double value)
{
    long long t = llround (value * 1000.0);
    const char* sign = t < 0 ? "-" : "";
    if (t < 0)
	t = -t;
    snprintf (out, n, "%s%lld.%03lld", sign, t / 1000, t % 1000);
}

static void RL_Append (char** p, char* end, const char* text)
{
    size_t n = strlen (text);
    if (n >= (size_t)(end - *p))
	n = (size_t)(end - *p) - 1;
    memcpy (*p, text, n);
    *p += n;
    **p = 0;
}

static void RL_AppendString (char** p, char* end, const char* key, const char* value)
{
    char escaped[512];
    fled_json_escape (escaped, sizeof escaped, value ? value : "");
    RL_Append (p, end, ",\"");
    RL_Append (p, end, key);
    RL_Append (p, end, "\":\"");
    RL_Append (p, end, escaped);
    RL_Append (p, end, "\"");
}

static void RL_AppendNumber (char** p, char* end, const char* key, double value)
{
    char text[32];
    RL_Fixed3 (text, sizeof text, value);
    RL_Append (p, end, ",\"");
    RL_Append (p, end, key);
    RL_Append (p, end, "\":");
    RL_Append (p, end, text);
}

static void RL_AppendInt (char** p, char* end, const char* key, long long value)
{
    char text[32];
    snprintf (text, sizeof text, "%lld", value);
    RL_Append (p, end, ",\"");
    RL_Append (p, end, key);
    RL_Append (p, end, "\":");
    RL_Append (p, end, text);
}

// {"start":a,"end":b} with b padded to a fixed width, so the header can be
// rewritten in place at the end of the run without moving a byte after it.
static void RL_AppendSpan (char** p, char* end, const char* key, double start, double last)
{
    char text[32], slot[32];
    RL_Append (p, end, ",\"");
    RL_Append (p, end, key);
    RL_Append (p, end, "\":{\"start\":");
    RL_Fixed3 (text, sizeof text, start);
    RL_Append (p, end, text);
    RL_Append (p, end, ",\"end\":");
    RL_Fixed3 (text, sizeof text, last);
    snprintf (slot, sizeof slot, "%-12s", text);
    RL_Append (p, end, slot);
    RL_Append (p, end, "}");
}

static void RL_BuildHeader (double load_end, double gpu_busy_end)
{
    const rl_facts_t* f = &rl.facts;
    char* p = rl.header;
    char* end = rl.header + sizeof rl.header;
    char text[64];

    RL_Append (&p, end, "{\"schema\":1");
    RL_AppendString (&p, end, "run_id", f->run_id);
    RL_AppendString (&p, end, "engine", "doom-ants");
    RL_Append (&p, end, ",\"tool\":{\"name\":\"linuxxdoom\",\"version\":");
    fled_json_escape (text, sizeof text, f->commit ? f->commit : "");
    RL_Append (&p, end, "\"");
    RL_Append (&p, end, text);
    RL_Append (&p, end, "\"}");
    RL_AppendString (&p, end, "build_type", f->build_type);
    RL_AppendString (&p, end, "commit", f->commit);
    RL_Append (&p, end, f->dirty ? ",\"dirty\":true" : ",\"dirty\":false");
    RL_AppendString (&p, end, "device", f->device);
    RL_AppendString (&p, end, "renderer", f->renderer);
    RL_AppendString (&p, end, "driver", f->driver);
    RL_AppendString (&p, end, "api", f->api);
    snprintf (text, sizeof text, ",\"resolution\":[%d,%d]", f->width, f->height);
    RL_Append (&p, end, text);
    RL_AppendNumber (&p, end, "render_scale", f->render_scale_pct / 100.0);
    RL_Append (&p, end, f->vsync ? ",\"vsync\":true" : ",\"vsync\":false");
    RL_Append (&p, end, f->pipelined ? ",\"pipelined\":true" : ",\"pipelined\":false");
    RL_AppendString (&p, end, "display", f->display);
    RL_AppendString (&p, end, "scene", f->scene);
    RL_Append (&p, end, ",\"inputs\":");
    RL_Append (&p, end, f->inputs_json ? f->inputs_json : "[]");
    RL_AppendString (&p, end, "started_utc", f->started_utc);
    RL_AppendString (&p, end, "tier", f->tier);
    RL_Append (&p, end, f->ray_tracing ? ",\"ray_tracing\":true" : ",\"ray_tracing\":false");
    if (f->frames_in_flight > 0)
	RL_AppendInt (&p, end, "frames_in_flight", f->frames_in_flight);
    RL_AppendInt (&p, end, "settling_frames", f->settling_frames);
    RL_AppendInt (&p, end, "settled_frames", f->settled_frames);
    if (f->gpu_tick_ns > 0)
	RL_AppendNumber (&p, end, "gpu_tick_ns", f->gpu_tick_ns);
    if (f->load_start >= 0)
	RL_AppendSpan (&p, end, "load", f->load_start, load_end);
    if (f->gpu_busy_start >= 0)
	RL_AppendSpan (&p, end, "gpu_busy_pct", f->gpu_busy_start, gpu_busy_end);
    RL_AppendString (&p, end, "os", f->os);
    RL_Append (&p, end, "}");
}

static void RL_Flush (void)
{
    const char* bytes;
    size_t n = fled_pending (&rl.writer, &bytes);
    if (n)
    {
	fwrite (bytes, 1, n, rl.file);
	fled_consume (&rl.writer, n);
    }
}

int RL_Open (FILE* f, const rl_facts_t* facts, size_t cap, long long frames)
{
    if (rl.active || !f)
	return 0;
    rl.buf = (char*)malloc (cap);
    if (!rl.buf)
	return 0;
    rl.file = f;
    rl.cap = cap;
    rl.facts = *facts;
    rl.frame = -1;
    rl.limit = frames;
    rl.segment[0] = rl.prev_segment[0] = 0;
    rl.fence_ms = rl.present_ms = 0.0;

    // The ends start at the opening values, so a run that dies early still has a
    // legal header, and the close line's absence is what says it died.
    RL_BuildHeader (facts->load_start, facts->gpu_busy_start);
    if (fled_open (&rl.writer, rl.buf, cap, facts->run_id, rl.header, NULL) != FLED_OK)
    {
	free (rl.buf);
	rl.buf = NULL;
	return 0;
    }
    rl.active = 1;
    return 1;
}

int RL_Active (void)
{
    return rl.active;
}

long long RL_CurrentFrame (void)
{
    return rl.active ? rl.frame : -1;
}

// The frame just ended, written all at once now its wall time is known.
static void RL_CloseFrame (double now_ms, int has_wall)
{
    const double wall = now_ms - rl.start_ms;

    fled_row (&rl.writer, rl.segment, rl.frame, "frame", "cpu_span", 0,
	      wall - rl.fence_ms - rl.present_ms);
    fled_row (&rl.writer, rl.segment, rl.frame, "frame", "fence_wait", 0, rl.fence_ms);
    fled_row (&rl.writer, rl.segment, rl.frame, "frame", "present_wait", 0, rl.present_ms);
    if (has_wall)
	fled_row (&rl.writer, rl.segment, rl.frame, "frame", "wall", 0, wall);
}

int RL_FrameBegin (double now_ms)
{
    if (!rl.active)
	return 0;

    // The readback of a frame's GPU timers happens while the NEXT frame presents.
    // So one frame past the limit is drawn and never written, purely so the last
    // recorded frame gets its GPU row, and that last frame has no frame/wall (FLED-0001
    // § 4.4). Without the extra frame every run would warn of a missing GPU result.
    if (rl.limit && rl.frame == rl.limit)
	return 1;

    if (rl.frame >= 0)
    {
	RL_CloseFrame (now_ms, !rl.limit || rl.frame + 1 < rl.limit);
	strcpy (rl.prev_segment, rl.segment);
    }

    rl.frame++;
    rl.start_ms = now_ms;
    rl.fence_ms = rl.present_ms = 0.0;

    // Half full is the moment to write: a whole frame's rows are far below that, so
    // no row is dropped, and the write lands between frames, not inside a pass.
    if (fled_pending (&rl.writer, NULL) > rl.cap / 2)
	RL_Flush ();
    return 0;
}

void RL_SetSegment (const char* segment)
{
    if (!rl.active || rl.frame < 0 || (rl.limit && rl.frame == rl.limit))
	return;
    if (strcmp (rl.segment, segment) != 0)
    {
	const int first = rl.segment[0] == 0 && rl.frame == 0;
	snprintf (rl.segment, sizeof rl.segment, "%s", segment);
	if (!first)
	    RL_Event ("cut", 1.0);
    }
}

void RL_CpuPass (const char* name, double ms)
{
    if (rl.active && rl.frame >= 0 && !(rl.limit && rl.frame == rl.limit))
	fled_row (&rl.writer, rl.segment, rl.frame, "cpu", name, 0, ms);
}

void RL_FenceWait (double ms)
{
    rl.fence_ms += ms;
}

void RL_PresentWait (double ms)
{
    rl.present_ms += ms;
}

void RL_Event (const char* name, double count)
{
    if (rl.active && rl.frame >= 0 && !(rl.limit && rl.frame == rl.limit))
	fled_row (&rl.writer, rl.segment, rl.frame, "event", name, 0, count);
}

static const char* RL_SegmentOf (long long frame)
{
    return frame == rl.frame ? rl.segment : rl.prev_segment;
}

void RL_GpuPass (long long frame, const char* name, int depth, double ms)
{
    if (rl.active && frame >= 0 && !(rl.limit && frame >= rl.limit))
	fled_row (&rl.writer, RL_SegmentOf (frame), frame, "gpu", name, depth, ms);
}

void RL_GpuSpan (long long frame, double ms)
{
    if (rl.active && frame >= 0 && !(rl.limit && frame >= rl.limit))
	fled_row (&rl.writer, RL_SegmentOf (frame), frame, "frame", "gpu_span", 0, ms);
}

void RL_Finish (double now_ms, double load_end, double gpu_busy_end)
{
    const int past_limit = rl.limit && rl.frame == rl.limit;
    long long frames;

    if (!rl.active)
	return;
    frames = past_limit ? rl.limit : rl.frame + 1;
    if (rl.frame >= 0 && !past_limit)
	RL_CloseFrame (now_ms, 0);		// a quit: the frame being drawn is the last
    fled_close (&rl.writer, frames);
    RL_Flush ();

    RL_BuildHeader (load_end >= 0 ? load_end : rl.facts.load_start,
		    gpu_busy_end >= 0 ? gpu_busy_end : rl.facts.gpu_busy_start);
    fflush (rl.file);
    if (fseek (rl.file, (long)strlen ("# frameledger "), SEEK_SET) == 0)
	fwrite (rl.header, 1, strlen (rl.header), rl.file);
    fflush (rl.file);

    free (rl.buf);
    rl.buf = NULL;
    rl.active = 0;
}

// SHA-256 (FIPS 180-4), so two runs can prove they loaded the same WAD. The
// format takes md5 or sha256; the engine had neither.
static const uint32_t rl_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define RL_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void RL_Sha256Block (uint32_t h[8], const unsigned char* b)
{
    uint32_t w[64], s[8], t1, t2;
    int i;

    for (i = 0; i < 16; i++)
	w[i] = (uint32_t)b[i*4] << 24 | (uint32_t)b[i*4+1] << 16 | (uint32_t)b[i*4+2] << 8 | b[i*4+3];
    for (i = 16; i < 64; i++)
	w[i] = w[i-16] + (RL_ROR(w[i-15], 7) ^ RL_ROR(w[i-15], 18) ^ (w[i-15] >> 3))
	     + w[i-7] + (RL_ROR(w[i-2], 17) ^ RL_ROR(w[i-2], 19) ^ (w[i-2] >> 10));
    memcpy (s, h, sizeof s);
    for (i = 0; i < 64; i++)
    {
	t1 = s[7] + (RL_ROR(s[4], 6) ^ RL_ROR(s[4], 11) ^ RL_ROR(s[4], 25))
	   + ((s[4] & s[5]) ^ (~s[4] & s[6])) + rl_k[i] + w[i];
	t2 = (RL_ROR(s[0], 2) ^ RL_ROR(s[0], 13) ^ RL_ROR(s[0], 22))
	   + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
	memmove (s + 1, s, 7 * sizeof s[0]);
	s[4] += t1;
	s[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
	h[i] += s[i];
}

int RL_HashFile (const char* path, char* out, long long* bytes)
{
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
		      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    unsigned char block[64];
    unsigned long long total = 0, bits;
    size_t n;
    int i;
    FILE* f = fopen (path, "rb");

    if (!f)
	return 0;
    while ((n = fread (block, 1, 64, f)) == 64)
    {
	RL_Sha256Block (h, block);
	total += 64;
    }
    fclose (f);
    total += n;

    // The padding: a 1 bit, zeros, then the length in bits, in one block or two.
    block[n++] = 0x80;
    if (n > 56)
    {
	memset (block + n, 0, 64 - n);
	RL_Sha256Block (h, block);
	n = 0;
    }
    memset (block + n, 0, 56 - n);
    bits = total * 8;
    for (i = 0; i < 8; i++)
	block[56 + i] = (unsigned char)(bits >> (56 - i * 8));
    RL_Sha256Block (h, block);

    strcpy (out, "sha256:");
    for (i = 0; i < 8; i++)
	snprintf (out + 7 + i * 8, 9, "%08x", (unsigned)h[i]);
    *bytes = (long long)total;
    return 1;
}
