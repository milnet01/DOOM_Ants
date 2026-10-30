// run_ledger_test.cpp — DOOM-0502: a benchmark run writes a legal frameledger file.
//
// Why this exists: the analyser's verdicts rest on rules a wrong row breaks with
// no error at all. A GPU row against the frame that read it back, rather than the
// one that submitted it, shifts every pass by a frame and still parses.
//
// Contract (frameledger FLED-0001):
//   1. The file opens with the header line and the column line, and ends with the
//      close line, carrying the frame count.
//   2. frame/wall is the time to the next frame's start; the last frame has none.
//   3. frame/cpu_span leaves out the fence and present waits.
//   4. A GPU row counts against the frame and segment that submitted the work.
//   5. A segment change writes event/cut on the new segment's first frame only.
//   6. With a frame limit, the extra drawn frame is never written, but the last
//      recorded frame still gets its GPU row.
//   7. RL_Finish rewrites the load readings without moving a byte after them.
//   8. A full buffer drops rows and counts them; it never loses the close line.
//   9. RL_HashFile matches the published SHA-256 vectors, at every padding length.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../run_ledger.c"
#include "check_util.h"

namespace {

rl_facts_t facts()
{
    rl_facts_t f = {};
    f.run_id = "run-1";
    f.commit = "abc1234";
    f.build_type = "release";
    f.device = "AMD \"RX\" 6600";
    f.renderer = "r";
    f.driver = "d";
    f.api = "vulkan-1.2";
    f.width = 1920;
    f.height = 1080;
    f.render_scale_pct = 50;
    f.pipelined = 1;
    f.display = "headless";
    f.scene = "e1m1";
    f.tier = "solid";
    f.ray_tracing = 1;
    f.os = "linux";
    f.started_utc = "2026-10-30T12:00:00Z";
    f.load_start = 0.25;
    f.gpu_busy_start = -1;
    return f;
}

std::string slurp(FILE* f)
{
    std::string out;
    char buf[4096];
    size_t n;
    rewind(f);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    return out;
}

bool has(const std::string& text, const std::string& line)
{
    return text.find(line) != std::string::npos;
}

std::string hash_of(const std::string& content)
{
    FILE* f = fopen("run_ledger_test.tmp", "wb");
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    char out[72] = "";
    long long bytes = -1;
    int ok = RL_HashFile("run_ledger_test.tmp", out, &bytes);
    remove("run_ledger_test.tmp");
    return ok && bytes == (long long)content.size() ? out : "";
}

}  // namespace

int main()
{
    // Three recorded frames, then the extra one.
    FILE* f = tmpfile();
    rl_facts_t r = facts();
    check(RL_Open(f, &r, 1 << 16, 3) == 1, "a run opens");
    check(RL_Active() == 1, "the run is active");
    check(RL_CurrentFrame() == -1, "no frame before the first RL_FrameBegin");

    check(RL_FrameBegin(100.0) == 0, "frame 0 begins");
    RL_SetSegment("level:e1m1");
    RL_CpuPass("game", 1.0);
    RL_FenceWait(2.0);
    RL_PresentWait(0.5);

    check(RL_FrameBegin(110.0) == 0, "frame 1 begins");
    check(RL_CurrentFrame() == 1, "the frame counter moved");
    RL_SetSegment("level:e1m2");        // as in the engine: the tics run before the readback
    RL_GpuPass(0, "trace", 0, 6.0);     // read back now, submitted by frame 0
    RL_GpuSpan(0, 7.0);

    check(RL_FrameBegin(120.0) == 0, "frame 2 begins");
    RL_GpuPass(1, "trace", 0, 8.0);
    RL_GpuSpan(1, 9.0);

    check(RL_FrameBegin(130.0) == 0, "the extra frame begins");
    RL_GpuPass(2, "trace", 0, 10.0);
    RL_GpuSpan(2, 11.0);
    RL_CpuPass("game", 99.0);           // the extra frame's own rows go nowhere
    RL_Event("tic", 99.0);
    RL_SetSegment("level:e9m9");

    check(RL_FrameBegin(140.0) == 1, "the limit says stop");
    RL_GpuPass(3, "trace", 0, 99.0);
    RL_Finish(140.0, 12.5, -1);
    check(RL_Active() == 0, "the run closed");

    const std::string text = slurp(f);
    fclose(f);

    // 1. The shape.
    check(text.rfind("# frameledger {\"schema\":1,\"run_id\":\"run-1\"", 0) == 0,
          "the header is the first line");
    check(has(text, "\nrun_id,segment,frame,category,name,depth,value\n"), "the column line");
    check(has(text, "\\\"RX\\\""), "a header string is JSON-escaped");
    check(text.size() > 60 && text.find("# frameledger-close {\"rows_dropped\":0,\"frames\":3}\n")
              == text.size() - strlen("# frameledger-close {\"rows_dropped\":0,\"frames\":3}\n"),
          "the close line is last and counts three frames");

    // 2. Wall.
    check(has(text, "run-1,level:e1m1,0,frame,wall,0,10.000\n"), "frame 0's wall");
    check(has(text, "run-1,level:e1m2,1,frame,wall,0,10.000\n"), "frame 1's wall");
    check(!has(text, ",2,frame,wall,"), "the last recorded frame has no wall");

    // 3. CPU span.
    check(has(text, "run-1,level:e1m1,0,frame,cpu_span,0,7.500\n"), "the span leaves out the waits");
    check(has(text, "run-1,level:e1m1,0,frame,fence_wait,0,2.000\n"), "the fence wait has a row");
    check(has(text, "run-1,level:e1m1,0,frame,present_wait,0,0.500\n"), "the present wait has a row");
    check(has(text, "run-1,level:e1m1,0,cpu,game,0,1.000\n"), "a CPU pass has a row");

    // 4. GPU rows.
    check(has(text, "run-1,level:e1m1,0,gpu,trace,0,6.000\n"), "frame 0 keeps its segment");
    check(has(text, "run-1,level:e1m1,0,frame,gpu_span,0,7.000\n"), "frame 0's GPU span");
    check(has(text, "run-1,level:e1m2,1,gpu,trace,0,8.000\n"), "frame 1's GPU row");

    // 5. Cuts.
    check(has(text, "run-1,level:e1m2,1,event,cut,0,1.000\n"), "the cut is on the new segment");
    check(!has(text, ",0,event,cut,"), "the run's first segment is not a cut");

    // 6. The extra frame.
    check(has(text, "run-1,level:e1m2,2,gpu,trace,0,10.000\n"), "the last frame got its GPU row");
    check(!has(text, ",3,"), "nothing is written against the extra frame");
    check(!has(text, "99.000"), "none of the extra frame's rows were written");
    check(!has(text, "e9m9"), "the extra frame's segment was not written");

    // 7. The rewrite.
    check(has(text, "\"load\":{\"start\":0.250,\"end\":12.500"), "the end load was written, wider than the start");
    check(has(text, "}\nrun_id,segment,"), "the header line still ends cleanly");

    // A quit, with no limit: the frame being drawn gets no wall.
    f = tmpfile();
    r = facts();
    check(RL_Open(f, &r, 1 << 16, 0) == 1, "a run with no limit opens");
    RL_FrameBegin(0.0);
    RL_FrameBegin(10.0);
    RL_Finish(25.0, -1, -1);
    {
        const std::string quit = slurp(f);
        check(has(quit, ",,0,frame,wall,0,10.000\n"), "the ended frame has a wall");
        check(!has(quit, ",1,frame,wall,"), "the last frame has none");
        check(has(quit, ",,1,frame,cpu_span,0,15.000\n"), "the last frame's work is still measured");
        check(has(quit, "\"frames\":2}"), "both frames are counted");
        check(has(quit, "\"end\":0.250"), "an unread end keeps the opening value");
    }
    fclose(f);

    // 8. A full buffer. RL_Flush runs only past half full, and only between frames,
    // so one frame's rows can still outrun a small buffer.
    f = tmpfile();
    r = facts();
    check(RL_Open(f, &r, 2048, 0) == 1, "a small buffer opens");
    RL_FrameBegin(0.0);
    for (int i = 0; i < 100; i++)
        RL_CpuPass("game", 1.0);
    RL_Finish(1.0, -1, -1);
    {
        const std::string full = slurp(f);
        check(has(full, "# frameledger-close {\"rows_dropped\":"), "the close line survived");
        check(!has(full, "\"rows_dropped\":0,"), "the dropped rows were counted");
    }
    fclose(f);

    // A failed open leaves nothing active.
    f = tmpfile();
    r = facts();
    check(RL_Open(f, &r, 16, 0) == 0, "a buffer too small is refused");
    check(RL_Active() == 0, "a refused run is not active");
    fclose(f);

    // 9. SHA-256: the FIPS 180-4 vectors, then lengths around the padding's edge,
    // checked against sha256sum.
    check(hash_of("") ==
          "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    check(hash_of("abc") ==
          "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    check(hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "the 56-byte vector, which needs a second padding block");
    check(hash_of(std::string(64, 'a')) ==
          "sha256:ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "exactly one block");
    check(hash_of(std::string(119, 'a')) ==
          "sha256:31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb",
          "one block and 55 bytes: the padding just fits");

    return check_summary("run_ledger_test");
}
