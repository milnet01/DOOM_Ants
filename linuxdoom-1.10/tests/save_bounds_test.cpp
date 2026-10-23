// save_bounds_test.cpp — DOOM-0255: a truncated .dsg must be refused, not read.
//
// Savegames are a trust boundary (docs/standards/security.md), and the load path
// had no idea where the file ended: G_DoLoadGame discarded M_ReadFile's length
// and every P_UnArchive* function advanced save_p on faith. The crafted input
// this pins is the simplest one there is — a .dsg that stops early.
//
// DOOM-0374 adds the write side, which had no check at all: G_DoSaveGame only
// compared the finished length against SAVEGAMESIZE, after every byte had already
// been written into the Z_Malloc block. The Writer walk below composes the same
// two decisions the way P_SaveRoom / P_SaveRoomAligned do.
//
// DOOM-0421 turned the write side's refusal into growth: where SaveFits says a
// write does not fit, P_SaveRoom reallocs to SaveGrowCapacity. The Writer walk
// still pins where SaveFits draws that line; the GrowingWriter walk composes the
// growth itself, the way P_SaveRoomPadded does.
//
// p_saveg.c itself cannot be unit tested (P_UnArchiveThinkers wants Z_Malloc, a
// loaded level and the thinker list), so the two decisions the cursor makes live
// in save_bounds.h and are tested here. The cursor walk below is not a
// re-implementation for its own sake: it composes those two exactly as
// P_SaveNeed / P_SaveNeedAligned do, so a case here fails for the same reason the
// engine would read out of bounds.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../save_bounds.h"
#include "check_util.h"

namespace {

// The load-path cursor, composed the way p_saveg.c composes it. `ok` going false
// is the engine's I_Error: the read was refused and nothing was dereferenced.
struct Cursor {
    const unsigned char* p;
    const unsigned char* end;
    bool ok = true;

    size_t remaining() const { return (size_t)(end - p); }

    // P_SaveNeed: require count bytes, no alignment step.
    bool need(size_t count)
    {
        if (!ok) return false;
        if (!SaveFits(remaining(), 0, count)) { ok = false; return false; }
        return true;
    }

    // P_SaveNeedAligned: the alignment step first, but only once the padding
    // itself has been shown to fit — applying it first is what walks the pointer
    // off the allocation on a file that ends mid-alignment.
    bool need_aligned(size_t count)
    {
        if (!ok) return false;
        size_t pad = SavePadBytes(p);
        if (!SaveFits(remaining(), pad, count)) { ok = false; return false; }
        p += pad;
        return true;
    }

    void consume(size_t count) { p += count; }
};

// A stand-in for one archived struct. The real ones are sizeof(mobj_t) etc.; the
// only property that matters here is that it is bigger than one byte and that
// the tag byte before it leaves the cursor misaligned, as tc_mobj does.
const size_t kRecord = 20;

// The save-path cursor, composed the way p_saveg.c composes it. `ok` going false
// is the engine's I_Error: the write was refused and nothing was written past the
// end of the buffer.
struct Writer {
    unsigned char* p;
    unsigned char* max;
    bool ok = true;

    size_t remaining() const { return (size_t)(max - p); }

    // P_SaveRoom: require room for count bytes, no alignment step.
    bool room(size_t count)
    {
        if (!ok) return false;
        if (!SaveFits(remaining(), 0, count)) { ok = false; return false; }
        return true;
    }

    // P_SaveRoomAligned: the padding is part of what must fit, for the same
    // reason it is on the read side — applying it first is what steps the cursor
    // past the allocation on a buffer that ends mid-alignment.
    bool room_aligned(size_t count)
    {
        if (!ok) return false;
        size_t pad = SavePadBytes(p);
        if (!SaveFits(remaining(), pad, count)) { ok = false; return false; }
        p += pad;
        return true;
    }

    void emit(size_t count) { p += count; }
};

// DOOM-0421: the save-path cursor as P_SaveRoomPadded composes it now — a write
// that does not fit reallocs the buffer to SaveGrowCapacity and moves the cursor
// with it. `ok` going false is the engine's out-of-memory I_Error.
struct GrowingWriter {
    unsigned char* base = nullptr;
    unsigned char* p = nullptr;
    unsigned char* max = nullptr;
    int regrows = 0;
    bool ok = true;

    explicit GrowingWriter(size_t capacity)
    {
        base = p = (unsigned char*)malloc(capacity);
        max = base + capacity;
    }
    ~GrowingWriter() { free(base); }

    void room_padded(size_t pad, size_t count)
    {
        if (!ok || SaveFits((size_t)(max - p), pad, count)) return;
        size_t used = (size_t)(p - base);
        size_t capacity = SaveGrowCapacity((size_t)(max - base), used, pad, count);
        unsigned char* grown = capacity ? (unsigned char*)realloc(base, capacity) : nullptr;
        if (!grown) { ok = false; return; }
        base = grown;
        p = grown + used;
        max = grown + capacity;
        regrows++;
    }

    void room(size_t count) { room_padded(0, count); }

    void room_aligned(size_t count)
    {
        size_t pad = SavePadBytes(p);
        room_padded(pad, count);
        p += pad;
    }
};

// Write `records` tag-plus-aligned-record pairs into a buffer that starts at
// `capacity` bytes, filling each record with its index, then read them all back
// from the final buffer. True only if every record survived every regrow intact
// and the cursor never passed the end of the buffer.
bool grow_and_verify(size_t capacity, int records, int* regrows)
{
    GrowingWriter w(capacity);
    size_t offsets[64];

    for (int i = 0; i < records; i++)
    {
        w.room(1);                          // the tag byte
        if (!w.ok) return false;
        *w.p++ = (unsigned char)0x1d;
        w.room_aligned(kRecord);
        if (!w.ok) return false;
        offsets[i] = (size_t)(w.p - w.base);
        memset(w.p, i + 1, kRecord);
        w.p += kRecord;
        if (w.p > w.max) return false;
    }
    *regrows = w.regrows;
    for (int i = 0; i < records; i++)
        for (size_t b = 0; b < kRecord; b++)
            if (w.base[offsets[i] + b] != (unsigned char)(i + 1)) return false;
    return true;
}

// Walk a buffer of `len` bytes the way P_UnArchiveThinkers does — a tag byte,
// then an aligned record, repeated — and report whether the cursor stayed inside
// it. Returns the number of records read before the walk stopped.
int walk_records(size_t len, int records, bool* stayed_in_bounds)
{
    unsigned char buf[64] = {0};
    Cursor c{buf, buf + len};
    int read = 0;

    for (int i = 0; i < records; i++)
    {
        if (!c.need(1)) break;              // the tag byte
        c.consume(1);
        if (!c.need_aligned(kRecord)) break;
        c.consume(kRecord);
        read++;
    }
    // The invariant every check above exists to keep.
    *stayed_in_bounds = (c.p <= c.end);
    return read;
}

// Write into a buffer of `len` bytes the way P_ArchiveThinkers does — a tag byte,
// then an aligned record, repeated — and report whether the cursor stayed inside
// it. Returns the number of records written before the walk stopped.
int write_records(size_t len, int records, bool* stayed_in_bounds)
{
    unsigned char buf[64] = {0};
    Writer w{buf, buf + len};
    int written = 0;

    for (int i = 0; i < records; i++)
    {
        if (!w.room(1)) break;              // the tag byte
        w.emit(1);
        if (!w.room_aligned(kRecord)) break;
        w.emit(kRecord);
        written++;
    }
    // The invariant DOOM-0374 adds: the buffer is never written past.
    *stayed_in_bounds = (w.p <= w.max);
    return written;
}

} // namespace

int main()
{
    // --- SavePadBytes: the four alignments the cursor can face. ---
    alignas(4) unsigned char buf[16] = {0};
    check_eq_int((long)SavePadBytes(buf + 0), 0, "an aligned cursor needs no padding");
    check_eq_int((long)SavePadBytes(buf + 1), 3, "one byte past aligned needs 3 bytes of padding");
    check_eq_int((long)SavePadBytes(buf + 2), 2, "two bytes past aligned needs 2");
    check_eq_int((long)SavePadBytes(buf + 3), 1, "three bytes past aligned needs 1");

    // --- SaveFits: the boundary itself. ---
    check(SaveFits(16, 0, 16) != 0, "a read that exactly fills the rest of the file is allowed");
    check(SaveFits(16, 0, 17) == 0, "a read one byte longer than the file is refused");
    check(SaveFits(0, 0, 1) == 0, "no read at all fits in an empty file");
    check(SaveFits(0, 0, 0) != 0, "a zero-length read fits in an empty file");

    // The case a naive `save_p + count <= save_end` check written AFTER the
    // alignment step gets wrong: the file ends inside the padding, so the
    // pointer is already out of the allocation by the time anything is compared.
    check(SaveFits(2, 3, 0) == 0, "padding alone can overrun a file that ends mid-alignment");
    check(SaveFits(3, 3, 0) != 0, "padding that exactly reaches the end of the file is allowed");
    check(SaveFits(3, 3, 1) == 0, "one byte after padding that reaches the end is refused");

    // Why SaveFits subtracts instead of adding: `pad + count <= remaining` wraps
    // here and reports a SIZE_MAX-byte read as fitting in 16 bytes.
    check(SaveFits(16, 1, SIZE_MAX) == 0, "a count near SIZE_MAX cannot wrap past the check");

    // --- The composed walk, on truncated buffers. ---
    bool in_bounds = false;

    // Whole records present: 1 tag + 3 pad + 20 payload = 24 bytes each.
    check_eq_int(walk_records(48, 2, &in_bounds), 2, "two whole records in a 48-byte file are read");
    check(in_bounds, "reading a complete file leaves the cursor inside it");

    // One byte short of the second record's payload.
    check_eq_int(walk_records(47, 2, &in_bounds), 1, "a file one byte short yields only the first record");
    check(in_bounds, "a file one byte short does not move the cursor past the end");

    // The file ends after the second tag byte, mid-alignment — the case that
    // walks the raw pointer off the allocation without this check.
    check_eq_int(walk_records(25, 2, &in_bounds), 1, "a file ending on a tag byte yields only the first record");
    check(in_bounds, "a file ending mid-alignment does not move the cursor past the end");

    // An empty file: not even the first tag byte.
    check_eq_int(walk_records(0, 2, &in_bounds), 0, "an empty file yields no records");
    check(in_bounds, "an empty file does not move the cursor past the end");

    // --- DOOM-0374: the same walk on the write side, which is where SaveFits
    // decides that the buffer must grow (DOOM-0421). ---
    bool wrote_in_bounds = false;

    check_eq_int(write_records(48, 2, &wrote_in_bounds), 2, "two whole records fit a 48-byte buffer");
    check(wrote_in_bounds, "filling the buffer exactly does not write past its end");

    // One byte short of the second record's payload.
    check_eq_int(write_records(47, 2, &wrote_in_bounds), 1, "a buffer one byte short takes only the first record");
    check(wrote_in_bounds, "a buffer one byte short is not written past");

    // The buffer ends just after the second tag byte, mid-alignment — the case
    // that steps the cursor past the allocation without this check, and that the
    // old post-hoc length comparison could only report once it had happened.
    check_eq_int(write_records(25, 2, &wrote_in_bounds), 1, "a buffer ending on a tag byte takes only the first record");
    check(wrote_in_bounds, "alignment padding cannot step the cursor past the end of the buffer");

    // A buffer with no room for even the first tag byte.
    check_eq_int(write_records(0, 2, &wrote_in_bounds), 0, "an empty buffer takes no records");
    check(wrote_in_bounds, "an empty buffer is not written to at all");

    // --- DOOM-0421: SaveGrowCapacity. ---
    check_eq_int((long)SaveGrowCapacity(16, 16, 0, 1), 32, "a full buffer doubles");
    check_eq_int((long)SaveGrowCapacity(16, 10, 3, 100), 128, "it doubles until the write fits");
    check_eq_int((long)SaveGrowCapacity(0, 0, 0, 5), 8, "an empty buffer grows from one byte");
    check(SaveGrowCapacity(16, 10, 1, SIZE_MAX) == 0, "a write that cannot be sized is refused, not wrapped");
    check(SaveGrowCapacity(16, 10, SIZE_MAX, 0) == 0, "padding that cannot be sized is refused too");
    check(SaveGrowCapacity(SIZE_MAX / 2 + 1, 10, 0, SIZE_MAX / 2 + 1) == SIZE_MAX / 2 + 11,
          "near SIZE_MAX it returns exactly what the write needs");

    // --- DOOM-0421: the growing walk. 1 tag + 3 pad + 20 payload = 24 bytes per
    // record, so 40 records need 960 bytes from a buffer that starts far smaller.
    int regrows = 0;
    check(grow_and_verify(48, 2, &regrows), "two records in a buffer they fit are written intact");
    check_eq_int(regrows, 0, "a save that fits never regrows");
    check(grow_and_verify(25, 40, &regrows), "forty records written through every regrow survive intact");
    check(regrows > 0, "a save past the starting size regrows instead of being refused");
    check(grow_and_verify(1, 40, &regrows), "a one-byte starting buffer still takes the whole save");

    return check_summary("save_bounds_test");
}
