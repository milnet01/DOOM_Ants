#include <cassert>
#include <cstring>
#include <cstdio>
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

int main() {
    test_hero_row();
    test_derive_row_blank_maps();
    test_comment_and_blank();
    test_flags_pom_and_nopom_clears_pom();
    test_malformed_wrong_column_count();
    printf("rb_materials parse: all passed\n");
    return 0;
}
