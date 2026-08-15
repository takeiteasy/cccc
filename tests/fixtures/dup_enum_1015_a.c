// #1015: first-listed TU declaring `enum E1015 { AA1015 = 1, BB1015 };` --
// no shared header, so keeper choice (rename_colliding_enum_constants(),
// src/serialize.c) falls through to lowest creation index, i.e. this file,
// being listed first on the command line. See
// tests/test_serialize_dup_enum_1015.c.
enum E1015 { AA1015 = 1, BB1015 };
int a_use_1015(void) { return AA1015 + BB1015; }
