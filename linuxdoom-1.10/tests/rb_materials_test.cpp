#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include "../rb_materials.h"   // header-only inline funcs; C++ TU, no extern "C"

static void test_hero_row() {
    rb_matrow_t r;
    int rc = rb_parse_material_line(
        "STARTAN3,hero,heroes/m/a.png,heroes/m/n.png,,,heroes/m/o.png,,heroes/m/h.png,2.0,pom", &r);
    assert(rc == 1);
    assert(strcmp(r.name, "STARTAN3") == 0);
    assert(r.is_hero == 1);
    assert(strcmp(r.maps[RB_ALB], "heroes/m/a.png") == 0);
    assert(strcmp(r.maps[RB_NRM], "heroes/m/n.png") == 0);
    assert(r.maps[RB_RGH][0] == '\0');           // blank cell = no map
    assert(strcmp(r.maps[RB_AO], "heroes/m/o.png") == 0);
    assert(strcmp(r.maps[RB_HGT], "heroes/m/h.png") == 0);
    assert(r.uv_scale == 2.0f);
    assert(r.flags == RB_FLAG_POM);
}

static void test_derive_row_blank_maps() {
    rb_matrow_t r;
    int rc = rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &r);
    assert(rc == 1);
    assert(r.is_hero == 0);
    for (int i = 0; i < RB_MAP_COUNT; i++) assert(r.maps[i][0] == '\0');
    assert(r.uv_scale == 1.0f);
    assert(r.flags == RB_FLAG_NOPOM);            // noPom set, pom not
}

static void test_comment_and_blank() {
    rb_matrow_t r;
    assert(rb_parse_material_line("#doom_name,source,...", &r) == 0);
    assert(rb_parse_material_line("   ", &r) == 0);
}

static void test_flags_pom_and_nopom_clears_pom() {
    assert(rb_parse_flags("pom") == RB_FLAG_POM);
    assert(rb_parse_flags("pom|noPom") == RB_FLAG_NOPOM);   // both -> noPom wins, pom cleared
    assert((rb_parse_flags("bogus") & RB_FLAG_BAD) != 0);
}

static void test_malformed_wrong_column_count() {
    rb_matrow_t r;
    assert(rb_parse_material_line("ONLY,three,cols", &r) == -1);
    assert(rb_parse_material_line("NAME,banana,,,,,,,,1.0,pom", &r) == -1); // unknown source
}

/* --- Task 3 additions --- */
#include <map>
#include <string>
static std::map<std::string,int>* g_names;      // test fixture: name -> id
static int test_resolver(const char* name, int* out_id) {
    auto it = g_names->find(name);
    if (it == g_names->end()) return 0;
    *out_id = it->second; return 1;
}
static void test_ctrl_table_build() {
    static_assert(sizeof(rb_matctrl_t) == 40, "std430 control struct must be 40 bytes");
    std::map<std::string,int> names = { {"STARTAN3", 5}, {"FLOOR4_8", 12} };
    g_names = &names;
    rb_matrow_t rows[2];
    assert(rb_parse_material_line("STARTAN3,hero,a.png,,,,,,,1.5,pom", &rows[0]) == 1);
    assert(rb_parse_material_line("FLOOR4_8,derive,,,,,,,,1.0,noPom", &rows[1]) == 1);
    rb_matctrl_t table[16];
    int dups = -1;
    rb_build_ctrl_table(rows, 2, 16, test_resolver, table, &dups);
    assert(dups == 0);
    assert(table[5].usePBR == 1 && table[5].flags == RB_FLAG_POM && table[5].uvScale == 1.5f);
    assert(table[12].usePBR == 1 && table[12].flags == RB_FLAG_NOPOM);
    assert(table[0].usePBR == 0);                // untouched material stays paletted
    for (int i = 0; i < RB_MAP_COUNT; i++) assert(table[5].maps[i] == -1); // slots unassigned yet
}
static void test_ctrl_table_dup_last_wins() {
    std::map<std::string,int> names = { {"TEKWALL1", 3} };
    g_names = &names;
    rb_matrow_t rows[2];
    rb_parse_material_line("TEKWALL1,hero,a.png,,,,,,,1.0,pom",   &rows[0]);
    rb_parse_material_line("TEKWALL1,derive,,,,,,,,4.0,noPom",     &rows[1]);
    rb_matctrl_t table[8]; int dups = 0;
    rb_build_ctrl_table(rows, 2, 8, test_resolver, table, &dups);
    assert(dups == 1);
    assert(table[3].uvScale == 4.0f && table[3].flags == RB_FLAG_NOPOM); // last row won
}

/* --- Task 2 coverage gap: three missing parser branches --- */
static void test_parser_coverage_gaps() {
    rb_matrow_t r;
    /* Blank uv_scale cell defaults to 1.0 */
    assert(rb_parse_material_line("NAME,hero,a.png,,,,,,,,pom", &r) == 1);
    assert(r.uv_scale == 1.0f);
    /* Unknown flags token returns -1 */
    assert(rb_parse_material_line("NAME,hero,a.png,,,,,,,1.0,bogus", &r) == -1);
    /* Too-many-columns row returns -1 */
    assert(rb_parse_material_line("A,hero,x,x,x,x,x,x,x,1.0,pom,extra", &r) == -1);
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
    printf("rb_materials: all passed\n");
    return 0;
}
