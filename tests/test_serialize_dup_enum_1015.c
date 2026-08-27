// CCCC_FLAGS: tests/fixtures/dup_enum_1015_a.c tests/fixtures/dup_enum_1015_b.c -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*enum E1015 \{\n    AA1015 = 1,)(?=[\s\S]*enum E1015__cccc_dup[0-9]+ \{\n    AA1015__cccc_dup[0-9]+ = 5,)
// CCCC_REJECT_STDOUT: AA1015 = 1,\n    BB1015 = 2\n\};\n\nenum E1015__cccc_dup[0-9]+ \{\n    AA1015 =
//
// #1015: follow-up to #1014's rename_colliding_type_tags() (src/serialize.c),
// which renames a colliding struct/union/enum *tag* apart across
// translation units but does nothing about a second, independent
// collision: two enums that share both a tag name and an enumerator name
// still produce "redefinition of enumerator 'AA1015'" from the host
// compiler after the tag rename, since only the tag spelling was renamed,
// not the enumerator identifiers.
//
// Fixed with rename_colliding_enum_constants() (src/serialize.c), run
// right after rename_colliding_type_tags() -- it groups every distinct
// complete enum Type and, for every enumerator name shared by two or more
// groups, renames every group's copy but one to `<name>__cccc_dup<N>` via
// a print-time lookup table (never by mutating EnumConstant.name itself,
// which would break same_type_or_origin()'s own enumerator comparison for
// every other consumer of that Type). Keeper selection mirrors #1014's own
// tiers so the two passes always agree on which group keeps the plain
// spelling.
//
// Verified this exact program printed `AA1015` unrenamed under both enums
// before this fix -- a host "redefinition of enumerator" compile error,
// even though #1014 had already renamed the colliding tag apart
// (`enum E1015__cccc_dup0`). tools/comptime_native_smoke.py's case 76 is
// the load-bearing proof that the resulting native binary actually links
// and runs, since a colliding enumerator is a host *compile* failure no -m
// shape assertion alone can see.
extern int a_use_1015(void);
extern int b_use_1015(void);

int main(void) {
    return a_use_1015() + b_use_1015() + 28;
}
