// #1016: an enum whose three enumerators each collide with a differently-
// shaped ordinary file-scope identifier declared in dup_enum_obj_1016_a.c
// (a static, an extern global, and a function respectively). Fixed by
// widening rename_colliding_enum_constants() (src/serialize.c) to also
// treat an emitted file-scope Obj name as occupying the ordinary-
// identifier namespace: the Obj is never renamed (renaming an external-
// linkage Obj would change the emitted symbol name), so every enumerator
// sharing its spelling is renamed instead. See
// tests/test_serialize_dup_enum_obj_1016.c.
enum E1016 { AA1016 = 100, BB1016 = 101, CC1016 = 102 };

int b_use_1016(void) {
    return AA1016 + BB1016 + CC1016;
}
