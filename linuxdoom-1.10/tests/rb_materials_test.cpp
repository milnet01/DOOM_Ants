// rb_materials_test.cpp — materials.csv parsing, the std430 control table, the
// traffic-ordered VRAM budget, and asset-path resolution (DOOM-0042).
//
// Uses check() rather than assert() so one broken case reports every OTHER case's
// result in the same run: this file holds ten independent tests, and under assert()
// a regression in the first hid the remaining nine until it was fixed and the suite
// re-run (2026-07-26 test audit). See tests/check_util.h.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD or GPU needed.
#include "check_util.h"

#include <cstdlib>   // setenv/unsetenv for the asset-root test
#include <cstring>
#include <map>
#include <string>
#include "../rb_materials.h"   // header-only inline funcs; C++ TU, no extern "C"

static void test_hero_row() {
    rb_matrow_t r;
    int rc = rb_parse_material_line(
        "STARTAN3,hero,heroes/m/a.png,heroes/m/n.png,,,heroes/m/o.png,,heroes/m/h.png,2.0,pom", &r);
    check(rc == 1, "a well-formed hero row parses");
    if (rc != 1) return;
    check(strcmp(r.name, "STARTAN3") == 0, "hero row: name column");
    check(r.is_hero == 1, "hero row: source 'hero' sets is_hero");
    check(strcmp(r.maps[RB_ALB], "heroes/m/a.png") == 0, "hero row: albedo map");
    check(strcmp(r.maps[RB_NRM], "heroes/m/n.png") == 0, "hero row: normal map");
    check(r.maps[RB_RGH][0] == '\0', "hero row: blank cell means no map");
    check(strcmp(r.maps[RB_AO], "heroes/m/o.png") == 0, "hero row: AO map");
    check(strcmp(r.maps[RB_HGT], "heroes/m/h.png") == 0, "hero row: height map");
    check(r.uv_scale == 2.0f, "hero row: uv_scale column");
    check(r.flags == RB_FLAG_POM, "hero row: pom flag");
}

static void test_derive_row_blank_maps() {
    rb_matrow_t r;
    int rc = rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &r);
    check(rc == 1, "a derive row with no maps parses");
    if (rc != 1) return;
    check(r.is_hero == 0, "derive row: is_hero clear");
    bool blank = true;
    for (int i = 0; i < RB_MAP_COUNT; i++) if (r.maps[i][0] != '\0') blank = false;
    check(blank, "derive row: every map cell is blank");
    check(r.uv_scale == 1.0f, "derive row: uv_scale column");
    check(r.flags == RB_FLAG_NOPOM, "derive row: noPom set, pom not");
}

static void test_comment_and_blank() {
    rb_matrow_t r;
    check(rb_parse_material_line("#doom_name,source,...", &r) == 0, "a comment line is skipped");
    check(rb_parse_material_line("   ", &r) == 0, "a whitespace-only line is skipped");
}

static void test_flags_pom_and_nopom_clears_pom() {
    check(rb_parse_flags("pom") == RB_FLAG_POM, "flags: pom");
    check(rb_parse_flags("pom|noPom") == RB_FLAG_NOPOM, "flags: noPom wins over pom and clears it");
    check((rb_parse_flags("bogus") & RB_FLAG_BAD) != 0, "flags: an unknown token sets RB_FLAG_BAD");
}

static void test_malformed_wrong_column_count() {
    rb_matrow_t r;
    check(rb_parse_material_line("ONLY,three,cols", &r) == -1, "too few columns is rejected");
    check(rb_parse_material_line("NAME,banana,,,,,,,,1.0,pom", &r) == -1, "unknown source is rejected");
}

/* --- Task 3 additions --- */
static std::map<std::string,int>* g_names;      // test fixture: name -> id
static std::multimap<std::string,int>* g_multi;   // optional: names with 2 ids
static int test_resolver(const char* name, int* out_ids, int max_ids) {
    if (g_multi) {
        int n = 0;
        auto r = g_multi->equal_range(name);
        for (auto it = r.first; it != r.second && n < max_ids; ++it) out_ids[n++] = it->second;
        return n;
    }
    if (!g_names) return 0;                     // fixture not installed: resolve nothing
    auto it = g_names->find(name);
    if (it == g_names->end()) return 0;
    out_ids[0] = it->second; return 1;
}
static void test_ctrl_table_build() {
    static_assert(sizeof(rb_matctrl_t) == 40, "std430 control struct must be 40 bytes");
    std::map<std::string,int> names = { {"STARTAN3", 5}, {"FLOOR4_8", 12} };
    g_names = &names;
    rb_matrow_t rows[2];
    int rc0 = rb_parse_material_line("STARTAN3,hero,a.png,,,,,,,1.5,pom", &rows[0]);
    int rc1 = rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &rows[1]);
    check(rc0 == 1 && rc1 == 1, "ctrl table: both source rows parse");
    if (rc0 == 1 && rc1 == 1) {
        rb_matctrl_t table[16];
        int dups = -1;
        rb_build_ctrl_table(rows, 2, 16, test_resolver, table, &dups);
        check(dups == 0, "ctrl table: no duplicate names");
        check(table[5].usePBR == 1 && table[5].flags == RB_FLAG_POM && table[5].uvScale == 1.5f,
              "ctrl table: hero row lands on its resolved id with its flags/scale");
        check(table[12].usePBR == 1 && table[12].flags == RB_FLAG_NOPOM,
              "ctrl table: derive row lands on its resolved id");
        check(table[0].usePBR == 0, "ctrl table: an unmentioned material stays paletted");
        bool unassigned = true;
        for (int i = 0; i < RB_MAP_COUNT; i++) if (table[5].maps[i] != -1) unassigned = false;
        check(unassigned, "ctrl table: texture slots start unassigned (-1)");
    }
    // The resolver reads this through a file-scope pointer (rb_name_resolver_t has
    // no userdata slot), and `names` dies with this frame -- clear it so a later
    // stray call fails loudly instead of reading a dangling stack address.
    g_names = nullptr;
}
static void test_ctrl_table_dup_last_wins() {
    std::map<std::string,int> names = { {"TEKWALL1", 3} };
    g_names = &names;
    rb_matrow_t rows[2];
    int rc0 = rb_parse_material_line("TEKWALL1,hero,a.png,,,,,,,1.0,pom",   &rows[0]);
    int rc1 = rb_parse_material_line("TEKWALL1,derive,,,,,,,,4.0,noPom",    &rows[1]);
    check(rc0 == 1 && rc1 == 1, "dup test: both source rows parse");
    if (rc0 == 1 && rc1 == 1) {
        rb_matctrl_t table[8]; int dups = 0;
        rb_build_ctrl_table(rows, 2, 8, test_resolver, table, &dups);
        check(dups == 1, "duplicate name is counted");
        check(table[3].uvScale == 4.0f && table[3].flags == RB_FLAG_NOPOM,
              "duplicate name: the last row wins");
    }
    g_names = nullptr;
}

/* DOOM-0410: a name that is both a wall texture and a flat gets HD on both. */
static void test_ctrl_table_every_match() {
    std::multimap<std::string,int> multi = { {"BOTHNAME", 4}, {"BOTHNAME", 9} };
    g_multi = &multi;
    rb_matrow_t row;
    check(rb_parse_material_line("BOTHNAME,hero,a.png,,,,,,,2.0,pom", &row) == 1,
          "every-match: the row parses");
    rb_matctrl_t table[12]; int dups = 0;
    rb_build_ctrl_table(&row, 1, 12, test_resolver, table, &dups);
    check(table[4].usePBR == 1 && table[9].usePBR == 1,
          "a row applies to EVERY id its name resolves to, not just the first");
    check(table[9].uvScale == 2.0f && table[9].flags == RB_FLAG_POM,
          "the second match carries the row's scale and flags too");
    check(dups == 0, "two ids from one row are not counted as duplicates");
    g_multi = nullptr;
}

/* DOOM-0410: values that would truncate or that fail every comparison. */
static void test_parser_rejects_what_it_used_to_truncate() {
    rb_matrow_t r;
    check(rb_parse_material_line("NAME,hero,a.png,,,,,,,nan,pom", &r) == 1 && r.uv_scale == 1.0f,
          "a NaN uv_scale falls back to 1.0 instead of reaching the shader");
    check(rb_parse_material_line("NAME,hero,a.png,,,,,,,inf,pom", &r) == 1 && r.uv_scale == 1.0f,
          "an infinite uv_scale falls back to 1.0");
    check(rb_parse_material_line("TOOLONGNAME,hero,a.png,,,,,,,1.0,pom", &r) == -1,
          "a name over 8 characters is malformed, not truncated to a different name");
    std::string longPath(200, 'p');
    std::string line = "NAME,hero," + longPath + ",,,,,,,1.0,pom";
    check(rb_parse_material_line(line.c_str(), &r) == -1,
          "a map path longer than its buffer is malformed, not truncated");
    std::string longFlags = "NAME,hero,a.png,,,,,,,1.0," + std::string(130, 'x');
    check(rb_parse_material_line(longFlags.c_str(), &r) == -1,
          "a flags field longer than its buffer is malformed");
}

/* --- Task 2 coverage gap: three missing parser branches --- */
static void test_parser_coverage_gaps() {
    rb_matrow_t r;
    int rc = rb_parse_material_line("NAME,hero,a.png,,,,,,,,pom", &r);
    check(rc == 1, "a blank uv_scale cell still parses");
    if (rc == 1) check(r.uv_scale == 1.0f, "a blank uv_scale cell defaults to 1.0");
    check(rb_parse_material_line("NAME,hero,a.png,,,,,,,1.0,bogus", &r) == -1,
          "an unknown flags token is rejected");
    check(rb_parse_material_line("A,hero,x,x,x,x,x,x,x,1.0,pom,extra", &r) == -1,
          "too many columns is rejected");
}

/* --- Task 5: Traffic-ordered budget --- */
static void test_budget_drops_lowest_traffic() {
    // 3 HD materials, each ~2 MB, ceiling 5 MB -> two fit, the third (lowest traffic) drops.
    rb_matctrl_t table[3];
    for (int i = 0; i < 3; i++) { table[i].usePBR = 1; table[i].flags = 0; table[i].uvScale = 1; }
    float traffic[3] = { 100.0f, 300.0f, 50.0f };   // id1 biggest, id2 smallest
    float est_mb[3]  = { 2.0f, 2.0f, 2.0f };
    int   is_hero[3] = { 0, 0, 0 };
    int order[3] = { -1, -1, -1 }, nloaded = -1;
    rb_apply_budget(table, 3, traffic, est_mb, is_hero, 5.0f, order, &nloaded);
    check(nloaded == 2, "budget: a 5 MB ceiling admits two 2 MB materials");
    check(order[0] == 1 && order[1] == 0, "budget: load order is descending traffic");
    check(table[1].usePBR == 1 && table[0].usePBR == 1, "budget: the two busiest stay HD");
    check(table[2].usePBR == 0, "budget: the lowest-traffic material is dropped");
}
static void test_budget_pins_hero_over_bigger_derived() {
    rb_matctrl_t table[2];
    for (int i = 0; i < 2; i++) { table[i].usePBR = 1; table[i].uvScale = 1; table[i].flags = 0; }
    float traffic[2] = { 10.0f, 999.0f };           // id1 huge traffic but derived
    float est_mb[2]  = { 4.0f, 4.0f };
    int   is_hero[2] = { 1, 0 };                     // id0 is a hero
    int order[2], nloaded = 0;
    rb_apply_budget(table, 2, traffic, est_mb, is_hero, 5.0f, order, &nloaded);
    check(nloaded == 1 && order[0] == 0, "budget: a hero is pinned ahead of a busier derived map");
    check(table[0].usePBR == 1 && table[1].usePBR == 0, "budget: the derived map is dropped");
}

/* --- Asset-root resolution (test audit 2026-07-26: was untested) ---------------
   rb_asset_path() joins $DOOMASSETDIR (or the built-in default) with a relative
   name. A missing or doubled separator here sends every HD material lookup to a
   path that does not exist, and only shows up as "no HD textures" on whichever
   machine has the variable set. */
static void test_asset_path() {
    char buf[256];

    unsetenv("DOOMASSETDIR");
    check(strcmp(rb_asset_root(), "assets/ultra/") == 0, "asset root: default when unset");
    rb_asset_path(buf, sizeof buf, "materials.csv");
    check(strcmp(buf, "assets/ultra/materials.csv") == 0,
          "asset path: default root already ends in '/', so no separator is added");

    setenv("DOOMASSETDIR", "/opt/hd", 1);
    rb_asset_path(buf, sizeof buf, "materials.csv");
    check(strcmp(buf, "/opt/hd/materials.csv") == 0,
          "asset path: a root without a trailing slash gets one");

    setenv("DOOMASSETDIR", "/opt/hd/", 1);
    rb_asset_path(buf, sizeof buf, "materials.csv");
    check(strcmp(buf, "/opt/hd/materials.csv") == 0,
          "asset path: a root with a trailing slash is not doubled");

    setenv("DOOMASSETDIR", "", 1);
    check(strcmp(rb_asset_root(), "assets/ultra/") == 0,
          "asset root: an empty variable falls back to the default");

    char tiny[8];
    unsetenv("DOOMASSETDIR");
    check(rb_asset_path(tiny, sizeof tiny, "materials.csv") == 0 && tiny[0] == '\0',
          "asset path: a result that would truncate is refused, not cut short");

    // DOOM-0410: the default follows the EXECUTABLE, not the launch directory. The
    // tests build into linuxdoom-1.10/linux/, the same place as the engine, so the
    // repo's own assets/ultra/ must be found from there.
    check(rb_asset_set_exe_dir(DOOM_TESTS_ROOT "/linux/") == 1,
          "asset root: the source tree's assets/ultra/ is found beside the executable");
    check(strstr(rb_asset_root(), "/linux/../../assets/ultra/") != nullptr,
          "asset root: with no DOOMASSETDIR, the executable-relative root is used");
    setenv("DOOMASSETDIR", "/opt/hd/", 1);
    check(strcmp(rb_asset_root(), "/opt/hd/") == 0,
          "asset root: DOOMASSETDIR still wins over the executable-relative root");
    unsetenv("DOOMASSETDIR");
    check(rb_asset_set_exe_dir("/nonexistent/dir/") == 0
          && strcmp(rb_asset_root(), "assets/ultra/") == 0,
          "asset root: a directory with no materials.csv is not adopted");
}

int main() {
    test_hero_row();
    test_derive_row_blank_maps();
    test_comment_and_blank();
    test_flags_pom_and_nopom_clears_pom();
    test_malformed_wrong_column_count();
    test_parser_coverage_gaps();
    test_ctrl_table_build();
    test_ctrl_table_dup_last_wins();
    test_ctrl_table_every_match();
    test_parser_rejects_what_it_used_to_truncate();
    test_budget_drops_lowest_traffic();
    test_budget_pins_hero_over_bigger_derived();
    test_asset_path();
    return check_summary("rb_materials");
}
