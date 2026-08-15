// #1016: an ordinary file-scope global with no name in common with the
// enum declared in enum_obj_no_collision_1016_b.c -- the control case for
// rename_colliding_enum_constants()'s widened Obj-name check, proving it
// doesn't rename anything when there's genuinely nothing to rename against.
// See tests/test_serialize_enum_obj_no_collision_1016.c.
int unrelated_global_1016 = 5;

int a_use_1016_nc(void) { return unrelated_global_1016; }
