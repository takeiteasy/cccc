// #1017: shared opaque-ish header, included by
// enum_hdr_collision_1017_a.c only -- enum_hdr_collision_1017_b.c
// deliberately does NOT include this header. See
// tests/test_serialize_enum_header_obj_collision_1017.c.
typedef enum { AA1017 = 1, BB1017 } E1017;
int use_e_1017(E1017 e);
