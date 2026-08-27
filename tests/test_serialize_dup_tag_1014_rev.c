// CCCC_FLAGS: tests/fixtures/dup_tag_1014_private.c tests/fixtures/dup_tag_1014_impl.c -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*struct DyGC1014 \{[\s\S]*int v;)(?=[\s\S]*struct DyGC1014__cccc_dup[0-9]+ \{[\s\S]*double d;)
// CCCC_REJECT_STDOUT: struct DyGC1014 \{[\s\S]*\};\n\nstruct DyGC1014 \{
//
// #1014's input-order-independence proof: the reverse of
// test_serialize_dup_tag_1014.c -- the private, non-header-exposed TU
// (fixtures/dup_tag_1014_private.c, input_files[0]) is listed and hence
// *created* before the header-exposed implementation TU
// (fixtures/dup_tag_1014_impl.c, input_files[1]). A first-created-wins-only
// keeper rule would pick the wrong group here and rename the
// header-exposed one instead, breaking the replayed header's own
// gc_open_1014/gc_val_1014 prototypes against a renamed struct (verified
// by hand: renaming a header-exposed group produces a host "conflicting
// types" error). rename_colliding_type_tags()'s tier-1 header_exposed
// check (src/serialize.c) is what keeps this test passing regardless of
// creation order -- it must win over the plain first-seen tie-break.
// tools/comptime_native_smoke.py's case is the load-bearing proof the
// resulting native binary actually links and runs.
#include "fixtures/dup_tag_1014.h"

int main(void) {
    DyGC1014 *g = gc_open_1014();
    return gc_val_1014(g);
}
