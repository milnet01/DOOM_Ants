// rb_model_test.cpp — DOOM-0539 INV-4: a malformed .dmdl is refused, never trusted.
//
// Why this exists: models are read from a folder the user can set (DOOMASSETDIR), so a
// file is untrusted input. A count believed before the size is checked reads out of bounds.
//
// Contract:
//   1. A well-formed file loads, and its vertices and lights read back in place.
//   2. Bad magic, a wrong version, counts over the caps, a vertex count not a multiple of 3,
//      a truncated or overlong body, and NaN or infinite values are each refused. So is a
//      finite value out of its range: a position past RB_MODEL_MAX_EXTENT, a normal not
//      near unit length, u or v outside 0..1, a light channel below 0 or over its cap, or a
//      reach of 0 or less or over its cap (DOOM-0547).
//   3. A refused file leaves the model empty.
//   4. A body at an odd offset in memory still loads.
//   5. A file on disk loads, and one past the largest valid size is refused.
//   6. A models.csv line gives a row, nothing, or malformed, and never cuts a path short.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../rb_model.h"
#include "check_util.h"

namespace {

// Every value is in range (DOOM-0547): vertex k sits at x = k % 64 with normal +z and
// v = (k % 8 + 1) / 8; a light sits at x 24 with colour (0.25, 0.5, 1) and reach 30.
std::vector<unsigned char> make(uint32_t version, uint32_t nv, uint32_t nl, size_t floatCount)
{
    static const float vert[8]  = { 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f };
    static const float light[7] = { 24.0f, 0.0f, 4.0f, 0.25f, 0.5f, 1.0f, 30.0f };
    std::vector<unsigned char> f(16 + floatCount * sizeof(float));
    std::memcpy(f.data(), "DMDL", 4);
    std::memcpy(f.data() + 4, &version, 4);
    std::memcpy(f.data() + 8, &nv, 4);
    std::memcpy(f.data() + 12, &nl, 4);
    for (size_t i = 0; i < floatCount; i++)
    {
        float v;
        if (i < (size_t)nv * 8)
        {
            const size_t k = i / 8, c = i % 8;
            v = c == 0 ? (float)(k % 64) : c == 7 ? (float)(k % 8 + 1) / 8.0f : vert[c];
        }
        else
            v = light[(i - (size_t)nv * 8) % 7];
        std::memcpy(f.data() + 16 + i * sizeof(float), &v, sizeof v);
    }
    return f;
}

std::vector<unsigned char> good(uint32_t nv = 3, uint32_t nl = 1)
{
    return make(1, nv, nl, nv * 8 + nl * 7);
}

bool refused(const std::vector<unsigned char>& f)
{
    rb_model_t m;
    const int ok = RB_ModelParse(f.data(), f.size(), &m);
    if (ok)
    {
        RB_ModelFree(&m);
        return false;
    }
    return m.numVerts == 0 && m.numLights == 0 && m.floats == nullptr;
}

void poison(std::vector<unsigned char>& f, size_t floatIndex, float v)
{
    std::memcpy(f.data() + 16 + floatIndex * sizeof(float), &v, sizeof v);
}

}  // namespace

int main()
{
    // 1.
    {
        const auto f = good();
        rb_model_t m;
        check(RB_ModelParse(f.data(), f.size(), &m) == 1, "a well-formed file loads");
        check(m.numVerts == 3 && m.numLights == 1, "its counts read back");
        check(RB_ModelVert(&m, 2)[0] == 2.0f && RB_ModelVert(&m, 2)[7] == 0.375f,
              "the last vertex reads back in place");
        check(RB_ModelLight(&m, 0)[0] == 24.0f, "the light starts straight after the vertices");
        check(RB_ModelLight(&m, 0)[6] == 30.0f, "the light's reach is its seventh float");
        RB_ModelFree(&m);
        check(m.floats == nullptr, "freeing empties the model");
    }
    {
        const auto f = good(900, 4);
        rb_model_t m;
        check(RB_ModelParse(f.data(), f.size(), &m) == 1, "a file exactly at both caps loads");
        RB_ModelFree(&m);
    }

    // 2.
    {
        auto f = good();
        f[0] = 'X';
        check(refused(f), "bad magic is refused");
    }
    check(refused(make(2, 3, 1, 3 * 8 + 7)), "a wrong version is refused");
    check(refused(good(903, 0)), "a vertex count over the cap is refused");
    check(refused(good(3, 5)), "a light count over the cap is refused");
    check(refused(good(0, 0)), "a model with no triangle is refused");
    check(refused(good(4, 0)), "a vertex count not a multiple of 3 is refused");
    // These two would overflow a 32-bit size sum if the caps came after the arithmetic.
    check(refused(make(1, 0xFFFFFFFDu, 0, 0)), "a huge vertex count with no body is refused");
    check(refused(make(1, 3, 0xFFFFFFFFu, 3 * 8)), "a huge light count is refused");
    {
        auto f = good();
        f.pop_back();
        check(refused(f), "a truncated body is refused");
        f.resize(10);
        check(refused(f), "a truncated header is refused");
        f.clear();
        check(refused(f), "an empty file is refused");
    }
    {
        auto f = good();
        f.push_back(0);
        check(refused(f), "an overlong body is refused");
    }
    {
        auto f = good();
        poison(f, 0, std::numeric_limits<float>::quiet_NaN());
        check(refused(f), "a NaN in a vertex is refused");
    }
    {
        auto f = good();
        poison(f, 3 * 8 + 6, std::numeric_limits<float>::infinity());
        check(refused(f), "an infinite value in the last light is refused");
    }
    // DOOM-0547: finite is not enough. Each field out of its range is refused on its own.
    {
        struct Case { size_t index; float value; const char* what; };
        const size_t L = 3 * 8;  // the light's first float
        const Case cases[] = {
            { 0, 3.0e38f, "a coordinate near FLT_MAX is refused" },
            { 2, -(RB_MODEL_MAX_EXTENT + 1.0f), "a coordinate past the extent is refused" },
            { 5, 2.0f, "a normal far from unit length is refused" },
            { 5, 0.0f, "a zero normal is refused" },
            { 6, 1.5f, "a u past 1 is refused" },
            { 15, -0.25f, "a v below 0 is refused" },
            { L + 1, RB_MODEL_MAX_EXTENT + 1.0f, "a light past the extent is refused" },
            { L + 4, -0.5f, "a negative light channel is refused" },
            { L + 5, RB_MODEL_MAX_LIGHT_RGB + 1.0f, "a light channel over the cap is refused" },
            { L + 6, 0.0f, "a zero reach is refused" },
            { L + 6, -30.0f, "a negative reach is refused" },
            { L + 6, RB_MODEL_MAX_REACH + 1.0f, "a reach over the cap is refused" },
        };
        for (const Case& c : cases)
        {
            auto f = good();
            poison(f, c.index, c.value);
            check(refused(f), c.what);
        }
        auto f = good();
        poison(f, 6, 1.0f);
        poison(f, 7, 0.0f);
        check(!refused(f), "u 1 and v 0, the ends of the range, load");
    }
    {
        rb_model_t m;
        check(RB_ModelParse(nullptr, 0, &m) == 0, "no data is refused");
    }

    // 4.
    {
        const auto f = good();
        std::vector<unsigned char> shifted(f.size() + 1);
        std::memcpy(shifted.data() + 1, f.data(), f.size());
        rb_model_t m;
        check(RB_ModelParse(shifted.data() + 1, f.size(), &m) == 1,
              "a file at an odd address loads");
        RB_ModelFree(&m);
    }

    // 5.
    {
        const auto f = good();
        std::FILE* t = std::tmpfile();
        std::fwrite(f.data(), 1, f.size(), t);
        std::rewind(t);
        rb_model_t m;
        check(RB_ModelRead(t, &m) == 1 && m.numVerts == 3, "a file on disk loads");
        RB_ModelFree(&m);
        std::fclose(t);
    }
    {
        // Valid header and body, one byte past the largest valid size: reading only as many
        // bytes as the buffer holds must not turn it back into a file that fits.
        auto f = good(900, 4);
        f.resize(RB_MODEL_MAX_FILE + 4096, 0);
        std::FILE* t = std::tmpfile();
        std::fwrite(f.data(), 1, f.size(), t);
        std::rewind(t);
        rb_model_t m;
        check(RB_ModelRead(t, &m) == 0 && m.floats == nullptr, "an oversized file is refused");
        std::fclose(t);
    }

    // 6.
    {
        char spr[5], file[16];
        check(RB_ModelCsvLine("BON1,models/B.dmdl\n", spr, file, sizeof file) == 1
                  && !std::strcmp(spr, "BON1") && !std::strcmp(file, "models/B.dmdl"),
              "a row gives its sprite and file");
        check(RB_ModelCsvLine("  # note\n", spr, file, sizeof file) == 0, "a comment is no row");
        check(RB_ModelCsvLine("\r\n", spr, file, sizeof file) == 0, "a blank line is no row");
        check(RB_ModelCsvLine("BON,x.dmdl\n", spr, file, sizeof file) == -1, "a short sprite name is malformed");
        check(RB_ModelCsvLine("BON1\n", spr, file, sizeof file) == -1, "no file column is malformed");
        check(RB_ModelCsvLine("BON1,\n", spr, file, sizeof file) == -1, "an empty file is malformed");
        check(RB_ModelCsvLine("BON1,a b\n", spr, file, sizeof file) == -1, "a second word is malformed");
        check(RB_ModelCsvLine("BON1,models/too-long-path.dmdl\n", spr, file, sizeof file) == -1,
              "a path too long is malformed, never cut short");
    }

    return check_summary("rb_model_test");
}
