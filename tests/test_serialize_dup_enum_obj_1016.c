// CCCC_FLAGS: tests/fixtures/dup_enum_obj_1016_a.c tests/fixtures/dup_enum_obj_1016_b.c -m -Wall
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: (?=[\s\S]*AA1016__cccc_dup[0-9]+ = 100,)(?=[\s\S]*BB1016__cccc_dup[0-9]+ = 101,)(?=[\s\S]*CC1016__cccc_dup[0-9]+ = 102)(?=[\s\S]*static int AA1016;)(?=[\s\S]*extern int BB1016;)(?=[\s\S]*int CC1016\(void\);)
// CCCC_REJECT_STDOUT: static int AA1016__cccc_dup|extern int BB1016__cccc_dup|int CC1016__cccc_dup
// #1017: -Wall added -- false-positive canary for -Wnative-name-collision.
// None of AA1016/BB1016/CC1016's enum group is header-exposed here (no
// #include at all in either fixture), so tier 1 never fires and the
// warning must not appear even though every enumerator collides with an
// Obj.
// CCCC_REJECT_STDERR: native-name-collision
//
// #1016: follow-up to #1014/#1015. rename_colliding_type_tags() (#1014)
// renames a colliding struct/union/enum *tag* against another tag, and
// rename_colliding_enum_constants() (#1015) renames a colliding
// *enumerator* against another enumerator -- but neither pass looked at
// the ordinary file-scope identifier namespace an enumerator also shares
// with a plain static/extern/function name, so `enum E { AA };` still
// collided with `static int AA;` (or an extern global, or a function of
// the same name) even after #1014/#1015 landed.
//
// Fixed by widening rename_colliding_enum_constants() (src/serialize.c)
// to also build the set of every emitted file-scope Obj name, and treat a
// name present in that set as occupying the ordinary-identifier namespace
// unconditionally -- no enum group keeps the plain spelling when an Obj
// already has it, since the Obj itself is never renamed (an external-
// linkage Obj's name is its emitted symbol; renaming it would break
// linking against anything outside these two translation units).
// Deliberately not restricted to *defining* Objs the way #1002's own scan
// is (#1002 cares about two definitions colliding) -- a bare prototype or
// `extern` declaration already occupies the namespace an enum constant
// shares, so it must be considered too.
//
// This fixture pair covers all three Obj shapes verified reproducing
// during investigation: a `static` variable, an external-linkage global,
// and a function. tools/comptime_native_smoke.py's case 77 is the
// load-bearing proof that the resulting native binary actually links and
// runs, since a colliding enumerator/identifier is a host *compile*
// failure no -m shape assertion alone can see.
//
// Verified this exact program printed unrenamed `AA1016`/`BB1016`/
// `CC1016` under the enum before this fix -- a host "redefinition of
// enumerator"/"conflicting types" compile error, even though the static
// and extern global (and the plain declaration) around them were
// otherwise fine.
extern int a_use_1016(void);
extern int b_use_1016(void);

int main(void) {
    return a_use_1016() - 19 + (b_use_1016() - 303) + 42;
}
