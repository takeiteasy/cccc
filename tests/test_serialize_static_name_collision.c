// CCCC_FLAGS: tests/fixtures/static_name_collision_1002_a.c -m
// CCCC_EXPECT_STDOUT: static int collide_1002__cccc_dup\d+\(void\)
//
// #1002: two different .c inputs, each independently defining
// `static int collide_1002(void)` with no shared header between them,
// compile and run correctly in the VM (each has its own Obj, internal
// linkage respected -- cc_link_progs deliberately never canonicalizes
// `static` symbols across TUs, #957's comment), but used to merge into one
// -c=native/-m output with two colliding definitions of the same
// identifier -- "redefinition of 'collide_1002'" from the host compiler.
// Fixed by rename_colliding_static_names() (serialize.c), which renames
// every same-named static Obj but the first when more than one distinct
// file defines it. Only the *second* TU's definition should be renamed --
// the fixture above (TU1) keeps its original name.
int collide_1002_call_a(void);
static int collide_1002(void) {
    return 22;
}

int main(void) {
    return collide_1002_call_a() + collide_1002();
}
