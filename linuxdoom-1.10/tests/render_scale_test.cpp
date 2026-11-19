// render_scale_test.cpp — DOOM-0622: the Render Scale row's sizes, and
// DOOM-0596: the Dynamic choice's controller.
//
// The menu and the renderer both reach these decisions through render_scale.h,
// which needs no GPU, so they are held here.
#include "../render_scale.h"
#include "check_util.h"

static int Width(int dispW, int dispH, int pct)
{
    int w, h;
    RsRenderSize(dispW, dispH, pct, &w, &h);
    return w;
}

static int Height(int dispW, int dispH, int pct)
{
    int w, h;
    RsRenderSize(dispW, dispH, pct, &w, &h);
    return h;
}

// A GPU whose frame costs `fixed` ms at any size plus `full` ms at 100%,
// following the pixel count -- or, with `steep`, costing three times that
// above 80%, a cliff the controller's model cannot foresee (the RX 6600 was
// measured growing faster than the pixel count). Runs the controller for
// `frames` and returns the number of moves it made.
static float Cost(int pct, float fixed, float full, bool steep)
{
    float s = (float) pct / 100.0f;
    return fixed + full * s * s * (steep && pct > 80 ? 3.0f : 1.0f);
}

static int Run(drs_t* d, float fixed, float full, float budget, int frames, bool steep = false)
{
    int moves = 0;
    for (int i = 0 ; i < frames ; i++)
    {
        int before = d->pct;
        if (DrsStep(d, Cost(before, fixed, full, steep), budget) != before)
            moves++;
    }
    return moves;
}

int main()
{
    int pcts[RS_MAX_SIZES];

    // ---- The row's sizes: the user's own examples (DOOM-0622) ----
    {
        int n = RsSizes(3840, 2160, pcts);
        check_eq_int(pcts[0], 100, "4K lists its own size first");
        check_eq_int(pcts[1], 83, "4K lists 3200x1800 as 83%");
        check_eq_int(pcts[2], 67, "4K lists 2560x1440 as 67%");
        check_eq_int(pcts[3], 50, "4K lists 1920x1080 as 50%");
        check_eq_int(pcts[n - 1], 33, "4K's smallest size is 1280x720, 33%");
        check_eq_int(Width(3840, 2160, 83), 3200, "83% of 4K is 3200 wide, not 3187");
        check_eq_int(Height(3840, 2160, 83), 1800, "83% of 4K is 1800 high, not 1792");
        check_eq_int(Width(3840, 2160, 33), 1280, "33% of 4K is 1280 wide");
        check_eq_int(Height(3840, 2160, 33), 720, "33% of 4K is 720 high");
    }
    {
        int n = RsSizes(1600, 1200, pcts);
        check(n >= 4, "a 4:3 screen lists at least four sizes");
        check_eq_int(Width(1600, 1200, pcts[1]), 1280, "1600x1200 lists 1280x960 next");
        check_eq_int(Height(1600, 1200, pcts[1]), 960, "1600x1200 lists 1280x960 next");
        check_eq_int(Width(1600, 1200, pcts[2]), 1024, "then 1024x768");
        check_eq_int(Width(1600, 1200, pcts[3]), 800, "then 800x600");
    }
    {
        RsSizes(2560, 1440, pcts);
        check_eq_int(pcts[1], 75, "at 1440p the percentage is of 1440p: 1080 is 75%");
        check_eq_int(Width(2560, 1440, 75), 1920, "75% of 1440p is 1920 wide");
    }

    // ---- Every listed size: in range, shrinking, and the output's shape ----
    {
        static const int outs[][2] = { {3840,2160}, {2560,1440}, {1920,1080}, {3440,1440},
                                       {2560,1600}, {1600,1200}, {1280,1024}, {1708,800},
                                       {640,400}, {320,200} };
        for (const auto& o : outs)
        {
            int n = RsSizes(o[0], o[1], pcts);
            check(n >= 3 && n <= RS_MAX_SIZES, "a screen lists three sizes or more");
            for (int i = 0 ; i < n ; i++)
            {
                int w = Width(o[0], o[1], pcts[i]), h = Height(o[0], o[1], pcts[i]);
                check(pcts[i] >= RS_MIN_PCT && pcts[i] <= 100, "a listed size is 33% to 100%");
                check(i == 0 || pcts[i] < pcts[i - 1], "the list shrinks, with no repeats");
                check(w >= 1 && w <= o[0] && h >= 1 && h <= o[1], "a size fits the screen");
                // Same shape as the output, to within a pixel of width.
                long skew = (long) w * o[1] - (long) h * o[0];
                check(skew <= o[1] && skew >= -(long) o[1], "a size keeps the screen's shape");
            }
            check_eq_int(Width(o[0], o[1], 100), o[0], "100% is the screen's own width");
            check_eq_int(Height(o[0], o[1], 100), o[1], "100% is the screen's own height");
        }
    }

    // ---- A percentage that names no standard size scales as it always did ----
    check_eq_int(Width(1708, 800, 50), 854, "50% of a 1708 window is 854");
    check_eq_int(Height(1708, 800, 50), 400, "50% of an 800 window is 400");
    check_eq_int(Height(1920, 1080, 75), 810, "75% of 1080 is 810: no standard size");
    check_eq_int(Height(1920, 1080, 5), 270, "a percentage under 25 draws at 25");
    check_eq_int(Height(1920, 1080, 500), 1080, "a percentage over 100 draws at 100");

    // ---- The row's cycle ----
    {
        int dyn = 1, pct = 50, n = RsSizes(3840, 2160, pcts);
        RsNextChoice(3840, 2160, &dyn, &pct);
        check(dyn == 0 && pct == 100, "after Dynamic comes the screen's own size");
        for (int i = 1 ; i < n ; i++)
        {
            RsNextChoice(3840, 2160, &dyn, &pct);
            check(dyn == 0 && pct == pcts[i], "then each size, largest first");
        }
        RsNextChoice(3840, 2160, &dyn, &pct);
        check(dyn == 1, "after the smallest size comes Dynamic again");

        dyn = 0; pct = 60;
        RsNextChoice(3840, 2160, &dyn, &pct);
        check(dyn == 0 && pct == 50, "a hand-edited 60 moves to the next size below it");
    }

    // ---- Dynamic: a frame over its budget drops the scale, and it settles ----
    {
        const float budget = 1000.0f / 30.0f, aim = budget * DRS_AIM;
        drs_t d;
        DrsReset(&d, 100);
        Run(&d, 4.0f, 60.0f, budget, 120);       // 64 ms at 100%: far over
        float s = (float) d.pct / 100.0f;
        check(4.0f + 60.0f * s * s <= aim, "it drops until the frame meets its aim");
        check(d.pct >= RS_MIN_PCT, "it never drops under the floor");
        check_eq_int(Run(&d, 4.0f, 60.0f, budget, 600), 0, "once there it holds still");
    }

    // ---- Dynamic: a drop takes a few slow frames in a row, and no more ----
    {
        const float budget = 1000.0f / 60.0f;
        drs_t d;
        DrsReset(&d, 80);
        for (int i = 0 ; i < 7 ; i++)
            check_eq_int(DrsStep(&d, 40.0f, budget), 80, "seven slow frames are not yet a drop");
        int dropped = DrsStep(&d, 40.0f, budget);
        check(dropped < 80, "the eighth slow frame in a row drops it");
        // The GPU time arrives a frame late, so the frames after a move still
        // report the old size's time. They must not drop it a second time.
        for (int i = 0 ; i < 6 ; i++)
            check_eq_int(DrsStep(&d, 40.0f, budget), dropped,
                         "a stale time just after a move is left out");
    }

    // ---- Dynamic: a lone slow frame is not the scene getting heavier ----
    //
    // The first build steered by an average and dropped the scale for every
    // hitch: 15 moves in 30 seconds at a size the GPU held easily. Hitches
    // came singly and in runs of four or five.
    {
        const float budget = 1000.0f / 30.0f;
        drs_t d;
        DrsReset(&d, 100);
        int moves = 0;
        for (int i = 0 ; i < 3000 ; i++)
            if (DrsStep(&d, i % 50 >= 45 ? 70.0f : 16.0f, budget) != 100)
                moves++;
        check_eq_int(moves, 0, "five frames in fifty at 70 ms leave a 16 ms scene alone");
    }

    // ---- Dynamic: room to spare raises it, but only once it has lasted ----
    {
        const float budget = 1000.0f / 60.0f;
        drs_t d;
        DrsReset(&d, 50);
        check_eq_int(Run(&d, 1.0f, 8.0f, budget, 29), 0,
                     "it does not rise before the wait is up");
        Run(&d, 1.0f, 8.0f, budget, 1200);       // 9 ms at 100%: fits easily
        check_eq_int(d.pct, 100, "a GPU with room ends at 100%");
        check_eq_int(Run(&d, 1.0f, 8.0f, budget, 600), 0, "and stays there");
    }

    // ---- Dynamic: a GPU steeper than the model does not make it flap ----
    //
    // The first build rose from 60% straight to 100%, found that too slow,
    // dropped, and did it again, twice a second, for as long as it ran.
    {
        const float budget = 1000.0f / 30.0f, aim = budget * DRS_AIM;
        drs_t d;
        DrsReset(&d, 50);
        Run(&d, 2.0f, 20.0f, budget, 1200, true);   // 22 ms at 100% but for the cliff
        check(Cost(d.pct, 2.0f, 20.0f, true) <= aim, "it settles under its aim");
        check(d.pct > 70 && d.pct <= 80, "and just under the cliff, not far below it");
        // Each retry costs four moves: up, down, and two steps back. The wait
        // doubles each time one fails, so 3000 frames hold two at most.
        check(Run(&d, 2.0f, 20.0f, budget, 3000, true) <= 8,
              "a size that proved too slow is retried only now and then");
    }
    {
        // A rise is a short step, never a leap to where the model points.
        drs_t d;
        DrsReset(&d, 60);
        for (int i = 0 ; i < 45 ; i++)
            DrsStep(&d, 1.0f, 1000.0f / 30.0f);
        check(d.pct > 60 && d.pct <= 65, "a rise is a short step");
    }

    // ---- Dynamic: a frame the floor cannot save stays at the floor ----
    {
        drs_t d;
        DrsReset(&d, 100);
        Run(&d, 30.0f, 200.0f, 1000.0f / 60.0f, 300);
        check_eq_int(d.pct, RS_MIN_PCT, "a hopeless frame sits at the floor");
    }

    // ---- Dynamic: between the two thresholds nothing moves ----
    {
        const float budget = 1000.0f / 30.0f, aim = budget * DRS_AIM;
        drs_t d;
        DrsReset(&d, 60);
        for (int i = 0 ; i < 600 ; i++)
            DrsStep(&d, aim * 0.9f, budget);
        check_eq_int(d.pct, 60, "a frame just under its aim leaves the scale alone");
    }

    // ---- Dynamic: a scene that gets heavier, then lighter, never flaps ----
    {
        const float budget = 1000.0f / 30.0f;
        drs_t d;
        DrsReset(&d, 50);
        Run(&d, 3.0f, 40.0f, budget, 1200);
        int light = d.pct;
        Run(&d, 3.0f, 90.0f, budget, 600);
        check(d.pct < light, "a heavier scene lowers the scale");
        check_eq_int(Run(&d, 3.0f, 90.0f, budget, 600), 0, "and it holds there");
        int heavy = d.pct;
        Run(&d, 3.0f, 40.0f, budget, 1800);
        // Not always to `light` exactly: anywhere between the two thresholds is settled.
        check(d.pct > heavy && d.pct <= light + DRS_MIN_STEP, "a lighter scene raises it again");
        check_eq_int(Run(&d, 3.0f, 40.0f, budget, 600), 0, "and it holds there too");
    }

    // ---- Dynamic: bad input changes nothing ----
    {
        drs_t d;
        DrsReset(&d, 70);
        check_eq_int(DrsStep(&d, 0.0f, 16.7f), 70, "a zero GPU time is ignored");
        check_eq_int(DrsStep(&d, -5.0f, 16.7f), 70, "a negative GPU time is ignored");
        check_eq_int(DrsStep(&d, 99.0f, 0.0f), 70, "a zero budget is ignored");
        d.pct = 7;
        check(DrsStep(&d, 1.0f, 16.7f) >= RS_MIN_PCT, "a scale under the floor is brought up");
    }

    return check_summary("render_scale_test");
}
