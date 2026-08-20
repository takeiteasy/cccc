// #1017: includes the shared header, making enum E1017's AA1017/BB1017
// header-exposed -- rename_colliding_type_tags()/rename_colliding_enum_
// constants()'s tier-1 rule (#1014/#1015) forbids renaming a header-exposed
// spelling, since the replayed #include binds it textually. See
// tests/test_serialize_enum_header_obj_collision_1017.c.
#include "enum_hdr_collision_1017.h"

int use_e_1017(E1017 e) {
    return (int)e + AA1017;
}
