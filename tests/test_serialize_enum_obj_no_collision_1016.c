// CCCC_FLAGS: tests/fixtures/enum_obj_no_collision_1016_a.c
// tests/fixtures/enum_obj_no_collision_1016_b.c -m -Wall CCCC_C4_SKIP:
// multi-source compile, not a single-TU bytecode round-trip CCCC_EXPECT_STDOUT:
// (?=[\s\S]*DD1016 = 1,)(?=[\s\S]*int unrelated_global_1016 = 5;)
// CCCC_REJECT_STDOUT: __cccc_dup
// #1017: -Wall added -- false-positive canary for -Wnative-name-collision.
// Nothing here collides with anything, so the warning must never appear.
// CCCC_REJECT_STDERR: native-name-collision
//
// #1016: the control case for rename_colliding_enum_constants()'s widened
// Obj-name check (src/serialize.c) -- an enum and an unrelated file-scope
// global that share no name at all must serialize byte-identically to
// before the fix, with no `__cccc_dup` rename appearing anywhere. This is
// the only test that would catch the new Obj-name set being built too
// eagerly (e.g. matching against the wrong Obj, or an off-by-one in the
// name comparison).
extern int a_use_1016_nc(void);
extern int b_use_1016_nc(void);

int main(void) {
    return a_use_1016_nc() + b_use_1016_nc() + 39;
}
