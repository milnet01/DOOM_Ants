// hdpack.cpp — DOOM-0632: the baker. Packs an asset root's PNGs into ultra.hdpack, each
// block-compressed for the GPU with its full mip chain (docs/specs/DOOM-0632-packed-hd-art.md
// § 4.3). A host tool: `make hdpack` builds it, and nothing links it into the game.
//
//   hdpack <asset-root> [--files <list>] [--out <pack>] [--jobs <n>]
//   hdpack --list <pack>
//
// Without --files it packs every PNG under the root except derived/. A --files list names
// the pictures, one path per line, relative to the root. A picture whose length, modification
// time and format match the pack already at --out keeps its encoded bytes.
//
// <asset-root>/hdpack_overrides.csv, when present, holds `path,plain` rows: that picture is
// stored uncompressed (the remedy when block compression spoils one, spec § 4.2).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

#include "bc7enc.h"
#include "rb_bc4.h"
#include "rb_hdpack.h"
#include "rb_image.h"

namespace {

namespace fs = std::filesystem;

int g_jobs = 1;

// Runs fn(row) for every row in [0, rows) across g_jobs threads. Rows are independent, so the
// output does not depend on the thread count.
template <typename Fn> void forRows(int rows, Fn fn)
{
    std::atomic<int> next{ 0 };
    auto work = [&] { for (int r; (r = next++) < rows;) fn(r); };
    std::vector<std::thread> pool;
    for (int i = 1; i < g_jobs; i++) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
}

// The picture's kind, from its name: the map suffix kHdV1Maps uses, or one of the two overlays.
// Returns 0 for a name that says nothing; that picture is left to the PNG path.
unsigned formatFor(const std::string& name, bool plain)
{
    static const struct { const char* tail; unsigned packed, unpacked; } kinds[] = {
        { "_alb.png",  RB_HDPACK_BC7_SRGB,  RB_HDPACK_RGBA8_SRGB },
        { "_emis.png", RB_HDPACK_BC7_SRGB,  RB_HDPACK_RGBA8_SRGB },
        { "_nrm.png",  RB_HDPACK_BC7_UNORM, RB_HDPACK_RGBA8_UNORM },
        { "_rgh.png",  RB_HDPACK_BC4_UNORM, RB_HDPACK_R8_UNORM },
        { "_met.png",  RB_HDPACK_BC4_UNORM, RB_HDPACK_R8_UNORM },
        { "_ao.png",   RB_HDPACK_BC4_UNORM, RB_HDPACK_R8_UNORM },
        { "_hgt.png",  RB_HDPACK_BC4_UNORM, RB_HDPACK_R8_UNORM },
        { "overlays/dirt.png",   RB_HDPACK_BC7_SRGB,  RB_HDPACK_RGBA8_SRGB },
        { "overlays/grunge.png", RB_HDPACK_BC4_UNORM, RB_HDPACK_R8_UNORM },
    };
    for (const auto& k : kinds)
    {
        const size_t n = std::strlen(k.tail);
        if (name.size() >= n && name.compare(name.size() - n, n, k.tail) == 0)
            return plain ? k.unpacked : k.packed;
    }
    return 0;
}

float srgbToLinear(int v)
{
    const float c = (float)v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

unsigned char linearToSrgb(float c)
{
    c = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f);
}

struct Rgba { int w, h; std::vector<unsigned char> px; };

// The next mip level: each texel the mean of the 2x2 above it (an odd edge repeats its last
// row or column). sRGB colour is averaged in linear light. Alpha is not used and stays 255.
Rgba halve(const Rgba& s, bool srgb)
{
    static float toLin[256];
    static bool  ready = false;
    if (!ready) { for (int i = 0; i < 256; i++) toLin[i] = srgbToLinear(i); ready = true; }

    Rgba d{ std::max(s.w / 2, 1), std::max(s.h / 2, 1), {} };
    d.px.resize((size_t)d.w * d.h * 4);
    forRows(d.h, [&](int y) {
        const int y0 = std::min(y * 2, s.h - 1), y1 = std::min(y * 2 + 1, s.h - 1);
        for (int x = 0; x < d.w; x++)
        {
            const int x0 = std::min(x * 2, s.w - 1), x1 = std::min(x * 2 + 1, s.w - 1);
            const unsigned char* t[4] = { &s.px[((size_t)y0 * s.w + x0) * 4], &s.px[((size_t)y0 * s.w + x1) * 4],
                                          &s.px[((size_t)y1 * s.w + x0) * 4], &s.px[((size_t)y1 * s.w + x1) * 4] };
            unsigned char* o = &d.px[((size_t)y * d.w + x) * 4];
            for (int c = 0; c < 3; c++)
                o[c] = srgb ? linearToSrgb((toLin[t[0][c]] + toLin[t[1][c]] + toLin[t[2][c]] + toLin[t[3][c]]) * 0.25f)
                            : (unsigned char)((t[0][c] + t[1][c] + t[2][c] + t[3][c] + 2) / 4);
            o[3] = 255;
        }
    });
    return d;
}

// One level's bytes as they go on disk. A block at the right or bottom edge of a level that is
// no multiple of four repeats the level's last column or row.
std::vector<unsigned char> encode(const Rgba& img, unsigned format)
{
    std::vector<unsigned char> out(RB_HdPackLevelBytes(format, (uint32_t)img.w, (uint32_t)img.h, 0));
    if (format == RB_HDPACK_RGBA8_SRGB || format == RB_HDPACK_RGBA8_UNORM)
        return img.px;
    if (format == RB_HDPACK_R8_UNORM)
    {
        for (size_t i = 0; i < out.size(); i++) out[i] = img.px[i * 4];
        return out;
    }
    const bool bc4 = format == RB_HDPACK_BC4_UNORM;
    const int  bw = (img.w + 3) / 4, bh = (img.h + 3) / 4, blockBytes = bc4 ? 8 : 16;
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);              // perceptual weights: colour
    if (format == RB_HDPACK_BC7_UNORM)
        bc7enc_compress_block_params_init_linear_weights(&params);   // a normal is not a colour
    forRows(bh, [&](int by) {
        for (int bx = 0; bx < bw; bx++)
        {
            unsigned char rgba[64], red[16];
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                {
                    const int sx = std::min(bx * 4 + x, img.w - 1), sy = std::min(by * 4 + y, img.h - 1);
                    const unsigned char* s = &img.px[((size_t)sy * img.w + sx) * 4];
                    std::memcpy(&rgba[(y * 4 + x) * 4], s, 4);
                    red[y * 4 + x] = s[0];
                }
            unsigned char* dst = &out[((size_t)by * bw + bx) * blockBytes];
            if (bc4) RB_Bc4Encode(red, dst);
            else     bc7enc_compress_block(dst, rgba, &params);
        }
    });
    return out;
}

struct Picture
{
    std::string name;
    unsigned    format = 0;
    int         w = 0, h = 0;
    uint64_t    srcBytes = 0;
    int64_t     srcMtime = 0;
    std::vector<std::vector<unsigned char>> levels;
};

bool stamp(const std::string& path, uint64_t* bytes, int64_t* mtime)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    *bytes = (uint64_t)st.st_size;
    *mtime = (int64_t)st.st_mtime;
    return true;
}

// Decodes, clamps as the loader does, builds the chain and encodes it. False with a message
// when the picture cannot be packed; it then loads from its PNG.
bool bake(const std::string& root, Picture& p)
{
    const std::string path = root + "/" + p.name;
    int w = 0, h = 0;
    if (!rb_image_info(path.c_str(), &w, &h) || w > (int)RB_HDPACK_MAX_EDGE || h > (int)RB_HDPACK_MAX_EDGE)
    {
        std::printf("hdpack: %s: unreadable or over %u px - left as a PNG.\n", p.name.c_str(), RB_HDPACK_MAX_EDGE);
        return false;
    }
    rb_image_t img;
    if (!rb_image_load(path.c_str(), &img)) { std::printf("hdpack: %s: would not decode.\n", p.name.c_str()); return false; }
    if (rb_image_downscale_max(&img, (int)RB_HDPACK_MAX_EDGE) == RB_DS_OOM) { rb_image_free(&img); return false; }

    Rgba level{ img.w, img.h, std::vector<unsigned char>(img.pixels, img.pixels + (size_t)img.w * img.h * 4) };
    rb_image_free(&img);
    for (size_t i = 3; i < level.px.size(); i += 4) level.px[i] = 255;
    p.w = level.w;
    p.h = level.h;
    const uint32_t count = RB_HdPackLevels((uint32_t)p.w, (uint32_t)p.h);
    for (uint32_t l = 0; l < count; l++)
    {
        if (l > 0) level = halve(level, RB_HdPackFormatSrgb(p.format));
        p.levels.push_back(encode(level, p.format));
    }
    return true;
}

// Takes p's encoded levels from the old pack when its stamp and format still match.
bool reuse(FILE* oldFile, const rb_hdpack_t& old, Picture& p)
{
    const rb_hdpack_entry_t* e = oldFile ? RB_HdPackFind(&old, p.name.c_str()) : nullptr;
    if (!e || e->srcBytes != p.srcBytes || e->srcMtime != p.srcMtime || e->format != p.format)
        return false;
    for (uint32_t l = 0; l < e->mipLevels; l++)
    {
        std::vector<unsigned char> d(RB_HdPackLevelBytes(e->format, e->width, e->height, l));
        if (!RB_HdPackReadLevel(oldFile, &old, e, l, d.data())) { p.levels.clear(); return false; }
        p.levels.push_back(std::move(d));
    }
    p.w = e->width;
    p.h = e->height;
    return true;
}

int list(const char* path)
{
    static const char* const names[] = { "?", "BC7-sRGB", "BC7", "BC4", "R8", "RGBA8-sRGB", "RGBA8" };
    FILE* f = std::fopen(path, "rb");
    rb_hdpack_t p;
    if (!f || !RB_HdPackOpen(f, &p))
    {
        std::fprintf(stderr, "hdpack: %s: missing, or refused by the reader.\n", path);
        if (f) std::fclose(f);
        return 1;
    }
    for (uint32_t i = 0; i < p.entryCount; i++)
    {
        const rb_hdpack_entry_t* e = &p.entries[i];
        std::printf("%.*s %s %ux%u %u\n", (int)e->nameLen, RB_HdPackEntryName(&p, e), names[e->format],
                    e->width, e->height, e->mipLevels);
    }
    RB_HdPackFree(&p);
    std::fclose(f);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && !std::strcmp(argv[1], "--list")) return list(argv[2]);
    if (argc < 2 || argv[1][0] == '-')
    {
        std::fprintf(stderr, "usage: hdpack <asset-root> [--files <list>] [--out <pack>] [--jobs <n>]\n"
                             "       hdpack --list <pack>\n");
        return 2;
    }
    const std::string root = argv[1];
    std::string out = root + "/" + RB_HDPACK_NAME, files;
    g_jobs = std::max(1u, std::thread::hardware_concurrency() / 2);   // leave the machine usable
    for (int i = 2; i + 1 < argc; i += 2)
    {
        if      (!std::strcmp(argv[i], "--files")) files = argv[i + 1];
        else if (!std::strcmp(argv[i], "--out"))   out = argv[i + 1];
        else if (!std::strcmp(argv[i], "--jobs"))  g_jobs = std::max(1, std::atoi(argv[i + 1]));
        else { std::fprintf(stderr, "hdpack: unknown option %s\n", argv[i]); return 2; }
    }

    // 1. Which pictures.
    std::vector<std::string> names;
    if (!files.empty())
    {
        std::ifstream in(files);
        if (!in) { std::fprintf(stderr, "hdpack: cannot read %s\n", files.c_str()); return 1; }
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) names.push_back(line);
    }
    else
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string rel = fs::relative(it->path(), root, ec).generic_string();
            if (it->is_regular_file(ec) && it->path().extension() == ".png" && rel.rfind("derived/", 0) != 0)
                names.push_back(rel);
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return RB_HdPackNameCmp(a.data(), a.size(), b.data(), b.size()) < 0;
    });
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::map<std::string, bool> plain;
    {
        std::ifstream in(root + "/hdpack_overrides.csv");
        for (std::string line; std::getline(in, line);)
        {
            const size_t comma = line.find(',');
            if (comma != std::string::npos && line.compare(comma + 1, std::string::npos, "plain") == 0)
                plain[line.substr(0, comma)] = true;
        }
    }

    // 2. Bake each, or carry it over from the pack already there.
    rb_hdpack_t old;
    std::memset(&old, 0, sizeof old);
    FILE* oldFile = std::fopen(out.c_str(), "rb");
    if (oldFile && !RB_HdPackOpen(oldFile, &old)) { std::fclose(oldFile); oldFile = nullptr; }

    bc7enc_compress_block_init();
    std::vector<Picture> pics;
    int baked = 0, kept = 0, skipped = 0;
    for (const std::string& name : names)
    {
        Picture p;
        p.name   = name;
        p.format = formatFor(name, plain.count(name) != 0);
        if (name.size() > RB_HDPACK_MAX_NAME || p.format == 0
            || !stamp(root + "/" + name, &p.srcBytes, &p.srcMtime))
        {
            std::printf("hdpack: %s: no known map suffix, or unreadable - left as a PNG.\n", name.c_str());
            skipped++;
            continue;
        }
        if (reuse(oldFile, old, p)) kept++;
        else if (bake(root, p))     baked++;
        else                        { skipped++; continue; }
        pics.push_back(std::move(p));
    }
    if (oldFile) { std::fclose(oldFile); RB_HdPackFree(&old); }

    // 3. Write beside the target, then swap it in, so a failed bake leaves the old pack.
    std::vector<std::vector<const unsigned char*>> ptrs(pics.size());
    std::vector<std::vector<uint32_t>> stored(pics.size());
    std::vector<std::vector<uint8_t>>  codec(pics.size());
    std::vector<rb_hdpack_src_t> srcs;
    for (size_t i = 0; i < pics.size(); i++)
    {
        for (const auto& d : pics[i].levels)
        {
            ptrs[i].push_back(d.data());
            stored[i].push_back((uint32_t)d.size());
            codec[i].push_back(RB_HDPACK_STORED);
        }
        srcs.push_back({ pics[i].name.c_str(), (uint8_t)pics[i].format, (uint16_t)pics[i].w, (uint16_t)pics[i].h,
                         pics[i].srcBytes, pics[i].srcMtime, ptrs[i].data(), stored[i].data(), codec[i].data() });
    }
    const std::string tmp = out + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    const bool wrote = f && RB_HdPackWrite(f, srcs.data(), (uint32_t)srcs.size());
    if ((f && std::fclose(f) != 0) || !wrote || std::rename(tmp.c_str(), out.c_str()) != 0)
    {
        std::fprintf(stderr, "hdpack: could not write %s\n", out.c_str());
        std::remove(tmp.c_str());
        return 1;
    }
    std::printf("hdpack: %s - %d baked, %d kept, %d left as PNG.\n", out.c_str(), baked, kept, skipped);
    return 0;
}
