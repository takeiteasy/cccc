// CCCC_FLAGS: tests/fixtures/dup_tag_1014_impl.c
// tests/fixtures/dup_tag_1014_pureuse.c tests/fixtures/dup_tag_1014_private.c
// -m CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: (?=[\s\S]*struct DyGC1014 \{[\s\S]*int
// v;)(?=[\s\S]*struct DyGC1014__cccc_dup[0-9]+ \{[\s\S]*double
// d;)(?=[\s\S]*struct DyGC1014 \*gc_open_1014\(void\)) CCCC_REJECT_STDOUT:
// DyGC1014__cccc_dup[0-9]+ \*g|struct DyGC1014 \{[\s\S]*\};\n\nstruct DyGC1014
// \{
//
// #1014's third TU shape: fixtures/dup_tag_1014_pureuse.c includes the
// shared header but never completes the tag itself -- its `DyGC1014 *g`
// local stays typed by an *incomplete* struct DyGC1014. same_type_or_
// origin() deliberately treats a tagged incomplete aggregate as equal to
// *either* complete shape sharing its tag (#892), so naively this
// incomplete Type could resolve (via find_tag_name()'s plain first-match
// scan) to whichever renamed group happens to be scanned first, spelling
// `g` as `struct DyGC1014__cccc_dup0 *` and calling gc_open_1014 (which
// really returns the header-exposed, plain-named group) through a silently
// wrong pointer type. find_tag_name()'s completeness-preferring first pass
// (guarded by SerializeContext.tag_renamed, src/serialize.c) is what
// routes this incomplete record to the correct (plain, header-exposed)
// group instead -- this is the one test that exercises that lookup change;
// test_serialize_dup_tag_1014.c/_rev.c both only ever see complete
// aggregates. tools/comptime_native_smoke.py's case is the load-bearing
// proof the resulting native binary actually links and runs.
extern int use_it_1014(void);

int main(void) {
    return use_it_1014();
}
