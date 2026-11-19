// hd_pack_test.cpp — DOOM-0632 INV-1 to INV-5: ultra.hdpack reads back as written, a malformed
// one is refused, a compressed level must unpack to its computed size, and the loader's
// take-it-from-the-pack decision holds.
//
// Why this exists: the pack is read from a folder the user can set (DOOMASSETDIR), so it is
// untrusted input. A size or offset believed before it is checked reads out of bounds.
//
// Contract:
//   1. A written pack reads back: each entry's name, format, size, level count, source stamp
//      and every level's bytes, for each format and for a size that is not a multiple of four.
//   2. One mutation per rule is refused, and the unmutated pack is accepted: bad magic, an
//      unknown version, a length claim unequal to the real length, a file over the size cap,
//      an entry count over its cap, tables past the end, an index cut short, a zero or oversized
//      edge, an unknown format, a level count that is not the full chain, a level run past
//      the mip table, a name past the name block, an empty or overlong name, names out of order or
//      repeated, a level starting inside the tables or ending past the file, a stored level
//      of the wrong length, an empty level, and an unknown codec.
//   3. A refused pack leaves the index empty.
//   4. The decision function refuses a wrong sRGB-ness, a block format on a device without
//      block compression, a codec the build lacks, and a PNG that is longer or re-dated; it
//      accepts an absent PNG and a matching one.
//   5. The writer refuses names out of order, and writing twice gives the same bytes.
//   6. An LZ4 level reads back as the bytes that were compressed. One whose stream unpacks
//      longer than the level's computed size, or shorter, is refused, and the bytes past the
//      level's size are never written.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../lz4.c"   // single-TU: the vendored unpacker rb_hdpack.h calls.
#include "../rb_hdpack.h"
#include "check_util.h"

namespace {

struct Pic
{
    std::string name;
    uint8_t     format;
    uint16_t    w, h;
    std::vector<std::vector<unsigned char>> levels;
    std::vector<uint32_t> stored;
    std::vector<uint8_t>  codec;
    std::vector<const unsigned char*> ptrs;
};

// Level l of picture `seed` is bytes (seed * 31 + l * 7 + i) & 255: distinct per level.
Pic pic(const char* name, uint8_t format, uint16_t w, uint16_t h, unsigned seed)
{
    Pic p{ name, format, w, h, {}, {}, {}, {} };
    for (uint32_t l = 0; l < RB_HdPackLevels(w, h); l++)
    {
        std::vector<unsigned char> d(RB_HdPackLevelBytes(format, w, h, l));
        for (size_t i = 0; i < d.size(); i++) d[i] = (unsigned char)(seed * 31 + l * 7 + i);
        p.stored.push_back((uint32_t)d.size());
        p.codec.push_back(RB_HDPACK_STORED);
        p.levels.push_back(d);
    }
    return p;
}

// Names ascend, one picture per format, and 10x6 is no multiple of four on either edge.
std::vector<Pic> pics()
{
    return {
        pic("heroes/a_alb.png",   RB_HDPACK_BC7_SRGB,    16, 8, 1),
        pic("heroes/a_hgt.png",   RB_HDPACK_R8_UNORM,    10, 6, 2),
        pic("heroes/a_nrm.png",   RB_HDPACK_BC7_UNORM,   10, 6, 3),
        pic("heroes/a_rgh.png",   RB_HDPACK_BC4_UNORM,   8, 8, 4),
        pic("models/SOUL_alb.png", RB_HDPACK_RGBA8_SRGB, 4, 2, 5),
        pic("overlays/grunge.png", RB_HDPACK_RGBA8_UNORM, 1, 1, 6),
    };
}

std::vector<rb_hdpack_src_t> srcsOf(std::vector<Pic>& ps)
{
    std::vector<rb_hdpack_src_t> s;
    for (Pic& p : ps)
    {
        p.ptrs.clear();
        for (auto& d : p.levels) p.ptrs.push_back(d.data());
        s.push_back({ p.name.c_str(), p.format, p.w, p.h, 1000u + p.w, 2000 + p.h,
                      p.ptrs.data(), p.stored.data(), p.codec.data() });
    }
    return s;
}

// The pack's bytes, or empty if the writer refused.
std::vector<unsigned char> bytesOf(std::vector<Pic> ps)
{
    std::vector<rb_hdpack_src_t> s = srcsOf(ps);
    std::vector<unsigned char> out;
    FILE* f = std::tmpfile();
    if (f && RB_HdPackWrite(f, s.data(), (uint32_t)s.size()))
    {
        out.resize((size_t)std::ftell(f));
        std::rewind(f);
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    if (f) std::fclose(f);
    return out;
}

template <typename T> void put(std::vector<unsigned char>& f, size_t at, T v)
{
    std::memcpy(f.data() + at, &v, sizeof v);
}
template <typename T> T get(const std::vector<unsigned char>& f, size_t at)
{
    T v;
    std::memcpy(&v, f.data() + at, sizeof v);
    return v;
}

bool accepted(const std::vector<unsigned char>& f, size_t avail, uint64_t fileBytes)
{
    rb_hdpack_t p;
    const int ok = RB_HdPackParse(f.data(), avail, fileBytes, &p);
    if (!ok)
        check(p.entries == nullptr && p.mips == nullptr && p.names == nullptr && p.entryCount == 0,
              "a refused pack leaves the index empty");
    RB_HdPackFree(&p);
    return ok != 0;
}
bool accepted(const std::vector<unsigned char>& f) { return accepted(f, f.size(), f.size()); }

// INV-3. Level 0 of a 16x8 RGBA8 picture is replaced by the LZ4 stream of `plainLen` bytes;
// only a stream of exactly the level's size may read back.
void lz4Level(size_t plainLen, bool wantOk, const char* what)
{
    std::vector<Pic> ps = { pic("heroes/a_alb.png", RB_HDPACK_RGBA8_UNORM, 16, 8, 9) };
    const size_t size = ps[0].levels[0].size();
    std::vector<unsigned char> plain(plainLen);
    for (size_t i = 0; i < plainLen; i++) plain[i] = (unsigned char)(i / 24);   // compressible
    std::vector<unsigned char> z(LZ4_compressBound((int)plainLen));
    z.resize(LZ4_compress_default((const char*)plain.data(), (char*)z.data(), (int)plainLen, (int)z.size()));
    ps[0].levels[0] = z;
    ps[0].stored[0] = (uint32_t)z.size();
    ps[0].codec[0]  = RB_HDPACK_LZ4;

    std::vector<rb_hdpack_src_t> s = srcsOf(ps);
    FILE* f = std::tmpfile();
    rb_hdpack_t p;
    const bool opened = f && !z.empty() && RB_HdPackWrite(f, s.data(), 1) && RB_HdPackOpen(f, &p);
    check(opened, "a pack with an LZ4 level opens");
    if (opened)
    {
        std::vector<unsigned char> got(size + 64, 0xEE);
        const bool ok = RB_HdPackReadLevel(f, &p, &p.entries[0], 0, got.data()) != 0;
        check(ok == wantOk, what);
        if (wantOk)
            check(std::memcmp(got.data(), plain.data(), size) == 0, "an LZ4 level's bytes read back");
        bool clean = true;
        for (size_t i = wantOk ? size : 0; i < got.size(); i++) clean = clean && got[i] == 0xEE;
        check(clean, "nothing is written past the level, or at all when it is refused");
        RB_HdPackFree(&p);
    }
    if (f) std::fclose(f);
}

void roundTrip()
{
    std::vector<Pic> ps = pics();
    std::vector<rb_hdpack_src_t> s = srcsOf(ps);
    FILE* f = std::tmpfile();
    check(f && RB_HdPackWrite(f, s.data(), (uint32_t)s.size()), "the writer writes a pack");
    rb_hdpack_t p;
    check(f && RB_HdPackOpen(f, &p), "a written pack opens");
    if (!f || !p.entries) { if (f) std::fclose(f); return; }

    check_eq_int(p.entryCount, (long)ps.size(), "every picture has an entry");
    for (const Pic& want : ps)
    {
        const rb_hdpack_entry_t* e = RB_HdPackFind(&p, want.name.c_str());
        check(e != nullptr, "a written name is found");
        if (!e) continue;
        check(std::string(RB_HdPackEntryName(&p, e), e->nameLen) == want.name, "the name reads back");
        check_eq_int(e->format, want.format, "the format reads back");
        check_eq_int(e->width, want.w, "the width reads back");
        check_eq_int(e->height, want.h, "the height reads back");
        check_eq_int(e->mipLevels, (long)want.levels.size(), "the level count reads back");
        check_eq_int((long)e->srcBytes, 1000 + want.w, "the source length reads back");
        check_eq_int((long)e->srcMtime, 2000 + want.h, "the source time reads back");
        for (uint32_t l = 0; l < e->mipLevels && l < want.levels.size(); l++)
        {
            std::vector<unsigned char> got(want.levels[l].size(), 0xEE);
            check(RB_HdPackReadLevel(f, &p, e, l, got.data()) && got == want.levels[l],
                  "a level's bytes read back");
        }
        std::vector<unsigned char> one(64);
        check(!RB_HdPackReadLevel(f, &p, e, e->mipLevels, one.data()), "a level past the chain is refused");
    }
    check(RB_HdPackFind(&p, "heroes/a") == nullptr, "a prefix of a name is not found");
    check(RB_HdPackFind(&p, "zz") == nullptr, "an absent name is not found");
    RB_HdPackFree(&p);
    std::fclose(f);
}

void refusals()
{
    const std::vector<unsigned char> good = bytesOf(pics());
    check(!good.empty() && accepted(good), "the unmutated pack is accepted");
    if (good.empty()) return;

    const uint32_t ne = get<uint32_t>(good, 8), nm = get<uint32_t>(good, 12), nb = get<uint32_t>(good, 16);
    const size_t   ent = 32, mip = ent + (size_t)ne * 32, names = mip + (size_t)nm * 16;
    const size_t   data = names + nb;
    // Entry 1 is the 10x6 R8 picture; its first mip record is `m1`.
    const size_t e1 = ent + 32, m1 = mip + (size_t)get<uint32_t>(good, e1 + 12) * 16;

    auto refused = [&](const char* what, auto mutate) {
        std::vector<unsigned char> f = good;
        mutate(f);
        check(!accepted(f), what);
    };

    refused("bad magic is refused", [](auto& f) { f[0] = 'X'; });
    refused("an unknown version is refused", [](auto& f) { put<uint32_t>(f, 4, 2); });
    refused("a wrong length claim is refused", [](auto& f) { put<uint64_t>(f, 24, f.size() + 1); });
    check(!accepted(good, good.size(), good.size() + 1), "a real length unequal to the claim is refused");
    {
        std::vector<unsigned char> f = good;
        put<uint64_t>(f, 24, (uint64_t)RB_HDPACK_MAX_FILE + 1);
        check(!accepted(f, f.size(), (uint64_t)RB_HDPACK_MAX_FILE + 1), "a file over the size cap is refused");
    }
    {
        // Only the header's own checks can refuse these: the tables are never reached.
        std::vector<unsigned char> h(good.begin(), good.begin() + 32);
        uint32_t a, b, c;
        put<uint64_t>(h, 24, data - 1);
        check(RB_HdPackHeader(h.data(), h.size(), data - 1, &a, &b, &c) == 0,
              "tables past the end of the file are refused");
        put<uint64_t>(h, 24, data);
        check(RB_HdPackHeader(h.data(), h.size(), data, &a, &b, &c) == data,
              "tables ending at the end of the file are accepted");
        put<uint32_t>(h, 8, RB_HDPACK_MAX_ENTRIES + 1);
        put<uint64_t>(h, 24, (uint64_t)RB_HDPACK_MAX_FILE);
        check(RB_HdPackHeader(h.data(), h.size(), RB_HDPACK_MAX_FILE, &a, &b, &c) == 0,
              "an entry count over its cap is refused");
    }
    check(!accepted(good, data - 1, good.size()), "an index cut short is refused");
    check(!accepted(good, 31, good.size()), "a header cut short is refused");

    // With every level marked compressed, no stored-length check stands in for these rules.
    auto packed = [&](std::vector<unsigned char>& f) {
        for (uint32_t i = 0; i < nm; i++) f[mip + (size_t)i * 16 + 12] = RB_HDPACK_LZ4;
    };
    {
        std::vector<unsigned char> f = good;
        packed(f);
        check(accepted(f), "a pack of compressed levels parses");
    }
    refused("a zero width is refused", [&](auto& f) {
        packed(f); put<uint16_t>(f, e1 + 8, 0); f[e1 + 7] = (unsigned char)RB_HdPackLevels(0, 6);
    });
    refused("a zero height is refused", [&](auto& f) {
        packed(f); put<uint16_t>(f, e1 + 10, 0); f[e1 + 7] = (unsigned char)RB_HdPackLevels(10, 0);
    });
    refused("an edge over the clamp is refused", [&](auto& f) {
        packed(f); put<uint16_t>(f, e1 + 8, RB_HDPACK_MAX_EDGE + 1);
        f[e1 + 7] = (unsigned char)RB_HdPackLevels(RB_HDPACK_MAX_EDGE + 1, 6);
    });
    refused("a height over the clamp is refused", [&](auto& f) {
        packed(f); put<uint16_t>(f, e1 + 10, RB_HDPACK_MAX_EDGE + 1);
        f[e1 + 7] = (unsigned char)RB_HdPackLevels(10, RB_HDPACK_MAX_EDGE + 1);
    });
    refused("format 0 is refused", [&](auto& f) { packed(f); f[e1 + 6] = 0; });
    refused("a format past the last is refused",
            [&](auto& f) { packed(f); f[e1 + 6] = RB_HDPACK_FORMAT_END; });
    refused("a short level count is refused", [&](auto& f) { f[e1 + 7]--; });
    refused("a long level count is refused", [&](auto& f) { f[e1 + 7]++; });
    refused("a level run past the mip table is refused",
            [&](auto& f) { packed(f); put<uint32_t>(f, e1 + 12, nm - 1); });
    refused("a first level past the mip table is refused", [&](auto& f) { put<uint32_t>(f, e1 + 12, nm + 1); });
    refused("a name running past the name block is refused", [&](auto& f) { put<uint32_t>(f, e1, nb); });
    refused("a name starting past the name block is refused",
            [&](auto& f) { put<uint32_t>(f, ent + (size_t)(ne - 1) * 32, nb + 1); });
    refused("an empty name is refused", [&](auto& f) { put<uint16_t>(f, ent + 4, 0); });   // entry 0
    refused("names out of order are refused", [&](auto& f) {
        // Swap entries 0 and 1 whole: each stays valid, only the order breaks.
        std::vector<unsigned char> a(f.begin() + ent, f.begin() + ent + 32);
        std::memcpy(f.data() + ent, f.data() + e1, 32);
        std::memcpy(f.data() + e1, a.data(), 32);
    });
    refused("a repeated name is refused", [&](auto& f) {
        put<uint32_t>(f, e1, get<uint32_t>(f, ent));
        put<uint16_t>(f, e1 + 4, get<uint16_t>(f, ent + 4));
    });

    refused("a level starting inside the tables is refused",
            [&](auto& f) { put<uint64_t>(f, m1, data - 1); });
    refused("a level ending past the file is refused",
            [&](auto& f) { put<uint64_t>(f, m1, f.size() - get<uint32_t>(f, m1 + 8) + 1); });
    refused("a level starting past the file is refused",
            [&](auto& f) { put<uint64_t>(f, m1, f.size() + 1); });
    refused("a stored level of the wrong length is refused",
            [&](auto& f) { put<uint32_t>(f, m1 + 8, get<uint32_t>(f, m1 + 8) - 1); });
    refused("an empty level is refused",
            [&](auto& f) { f[m1 + 12] = RB_HDPACK_LZ4; put<uint32_t>(f, m1 + 8, 0); });
    refused("an unknown codec is refused", [&](auto& f) { f[m1 + 12] = RB_HDPACK_CODEC_END; });

    // A compressed level may be any length inside the data; only its unpacked length is fixed.
    {
        std::vector<unsigned char> f = good;
        f[m1 + 12] = RB_HDPACK_LZ4;
        put<uint32_t>(f, m1 + 8, get<uint32_t>(f, m1 + 8) - 1);
        check(accepted(f), "a compressed level shorter than its unpacked size parses");
    }
}

void decision()
{
    std::vector<unsigned char> f = bytesOf(pics());
    const size_t mip = 32 + (size_t)get<uint32_t>(f, 8) * 32;
    f[mip + 12] = RB_HDPACK_LZ4;   // the first picture's level 0, for the codec case
    rb_hdpack_t p;
    check(RB_HdPackParse(f.data(), f.size(), f.size(), &p), "the decision pack parses");
    if (!p.entries) return;
    const rb_hdpack_entry_t* alb = RB_HdPackFind(&p, "heroes/a_alb.png");    // BC7 sRGB, LZ4 level
    const rb_hdpack_entry_t* nrm = RB_HdPackFind(&p, "heroes/a_nrm.png");    // BC7 linear
    const rb_hdpack_entry_t* hgt = RB_HdPackFind(&p, "heroes/a_hgt.png");    // R8 linear
    if (!alb || !nrm || !hgt) { check(false, "the decision entries are found"); RB_HdPackFree(&p); return; }
    const unsigned stored = 1u << RB_HDPACK_STORED, all = stored | 1u << RB_HDPACK_LZ4;

    check(RB_HdPackUsable(&p, nrm, 0, 1, stored, 0, 0, 0), "an absent PNG lets the pack serve");
    check(RB_HdPackUsable(&p, nrm, 0, 1, stored, 1, nrm->srcBytes, nrm->srcMtime),
          "a matching PNG lets the pack serve");
    check(!RB_HdPackUsable(&p, nrm, 0, 1, stored, 1, nrm->srcBytes + 1, nrm->srcMtime),
          "a longer PNG wins over the pack");
    check(!RB_HdPackUsable(&p, nrm, 0, 1, stored, 1, nrm->srcBytes, nrm->srcMtime + 1),
          "a re-dated PNG wins over the pack");
    check(!RB_HdPackUsable(&p, nrm, 1, 1, stored, 0, 0, 0), "a linear entry is not used as sRGB");
    check(!RB_HdPackUsable(&p, alb, 0, 1, all, 0, 0, 0), "an sRGB entry is not used as linear");
    check(RB_HdPackUsable(&p, alb, 1, 1, all, 0, 0, 0), "an sRGB entry is used as sRGB");
    check(!RB_HdPackUsable(&p, nrm, 0, 0, stored, 0, 0, 0),
          "a block format is not used without block compression");
    check(RB_HdPackUsable(&p, hgt, 0, 0, stored, 0, 0, 0),
          "an uncompressed format is used without block compression");
    check(!RB_HdPackUsable(&p, alb, 1, 1, stored, 0, 0, 0), "a codec the build lacks is not used");

    FILE* tf = std::tmpfile();
    std::vector<unsigned char> buf(4096);
    check(tf && std::fwrite(f.data(), 1, f.size(), tf) == f.size()
              && !RB_HdPackReadLevel(tf, &p, alb, 0, buf.data()),
          "a compressed level is not read as stored");
    if (tf) std::fclose(tf);
    RB_HdPackFree(&p);
}

// A name one byte over the limit, in a pack that is otherwise well formed: the writer would
// refuse it, so the limit is raised by hand in a pack written with a shorter name.
void longName()
{
    std::vector<Pic> ps = { pic(std::string(RB_HDPACK_MAX_NAME, 'n').c_str(), RB_HDPACK_R8_UNORM, 1, 1, 7) };
    std::vector<unsigned char> f = bytesOf(ps);
    check(!f.empty() && accepted(f), "a name at the length limit is accepted");
    if (f.empty()) return;
    // Grow the name block by one byte, in place: every later offset moves up by one.
    const size_t names = 32 + 32 + 16;
    f.insert(f.begin() + names, 'n');
    put<uint32_t>(f, 16, RB_HDPACK_MAX_NAME + 1);
    put<uint64_t>(f, 24, f.size());
    put<uint16_t>(f, 32 + 4, RB_HDPACK_MAX_NAME + 1);
    put<uint64_t>(f, 32 + 32, get<uint64_t>(f, 32 + 32) + 1);
    check(!accepted(f), "a name over the length limit is refused");
    ps[0].name += 'n';
    check(bytesOf(ps).empty(), "the writer refuses a name over the length limit");
}

void writer()
{
    std::vector<Pic> ps = pics();
    check(bytesOf(ps) == bytesOf(ps) && !bytesOf(ps).empty(), "writing twice gives the same bytes");
    std::swap(ps[0], ps[1]);
    check(bytesOf(ps).empty(), "the writer refuses names out of order");
    ps = pics();
    ps[1].name = ps[0].name;
    check(bytesOf(ps).empty(), "the writer refuses a repeated name");
}

}  // namespace

int main()
{
    roundTrip();
    refusals();
    decision();
    longName();
    writer();
    lz4Level(16 * 8 * 4, true, "an LZ4 level of the computed size reads");
    lz4Level(16 * 8 * 4 + 64, false, "an LZ4 level that unpacks long is refused");
    lz4Level(16 * 8 * 4 - 12, false, "an LZ4 level that unpacks short is refused");
    return check_summary("hd_pack_test");
}
