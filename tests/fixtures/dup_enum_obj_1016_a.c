// #1016: three ordinary file-scope identifiers -- a static definition, an
// extern (external-linkage) global definition, and a function -- each
// sharing a spelling with an enumerator declared in
// dup_enum_obj_1016_b.c. Neither rename_colliding_static_names() (#1002,
// which only ever renames a colliding *Obj* against another Obj) nor
// rename_colliding_enum_constants() (#1015, which only ever renamed a
// colliding enumerator against another enumerator) looked at the other's
// namespace -- C has one ordinary identifier namespace at file scope, so
// an enumerator and a plain identifier collide there too. See
// tests/test_serialize_dup_enum_obj_1016.c.
static int AA1016 = 3;
int        BB1016 = 7;
int CC1016(void) {
    return 9;
}

int a_use_1016(void) {
    return AA1016 + BB1016 + CC1016();
}
