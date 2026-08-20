// #1016: an enum with no enumerator name shared by
// enum_obj_no_collision_1016_a.c's global. See
// tests/test_serialize_enum_obj_no_collision_1016.c.
enum ENoCollide1016 { DD1016 = 1, EE1016 = 2 };

int b_use_1016_nc(void) {
    return DD1016 + EE1016;
}
