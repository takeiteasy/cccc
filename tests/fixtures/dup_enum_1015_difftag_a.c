// #1015: two enums with *different* tags (E1015DTA vs E1015DTB below) can
// still share a colliding enumerator name -- outside #1014's own repro
// wording (which was tag-collision-shaped) but the identical host-compiler
// failure and the identical mechanism, so rename_colliding_enum_constants()
// (src/serialize.c) groups by enum Type identity, not by whether the
// enclosing tags themselves collided. See
// tests/test_serialize_dup_enum_1015_difftag.c.
enum E1015DTA { AA1015DT = 1, BB1015DT };
int a_use_1015dt(void) {
    return AA1015DT + BB1015DT;
}
