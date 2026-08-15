// CCCC_FLAGS: tests/fixtures/dup_enum_1015_difftag_a.c tests/fixtures/dup_enum_1015_difftag_b.c -m
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: (?=[\s\S]*enum E1015DTA \{\n    AA1015DT = 1,)(?=[\s\S]*enum E1015DTB \{\n    AA1015DT__cccc_dup[0-9]+ = 5,)
// CCCC_REJECT_STDOUT: enum E1015DTB \{\n    AA1015DT =
//
// #1015 widening: two enums with *different* tags (so #1014's tag-rename
// pass never fires -- E1015DTA and E1015DTB don't collide) can still
// declare a same-named enumerator (AA1015DT). rename_colliding_enum_
// constants() (src/serialize.c) groups by enum Type identity via
// same_type_strong(), not by whether the enclosing tag itself collided, so
// this shape is caught by the same pass with no extra code.
//
// Verified this exact program printed `AA1015DT` unrenamed under both
// enums before this fix -- a host "redefinition of enumerator" compile
// error, with #1014's own tag-collision machinery never even engaging
// (E1015DTA/E1015DTB are two distinct, non-colliding tags).
extern int a_use_1015dt(void);
extern int b_use_1015dt(void);

int main(void) {
    return a_use_1015dt() + b_use_1015dt() + 28;
}
