// #1017: a plain file-scope static sharing enum_hdr_collision_1017_a.c's
// header-exposed enumerator AA1017's spelling, declared in a TU that does
// NOT include enum_hdr_collision_1017.h. rename_colliding_enum_constants()
// (#1016) never renames an Obj, and tier 1 (#1014/#1015) never renames a
// header-exposed enumerator -- so this collision is genuinely
// unrepresentable in flat C, left for the host compiler to report.
// -Wnative-name-collision (#1017) points at it here first. See
// tests/test_serialize_enum_header_obj_collision_1017.c.
static int AA1017 = 3;

int b_use_1017(void) {
    return AA1017;
}
