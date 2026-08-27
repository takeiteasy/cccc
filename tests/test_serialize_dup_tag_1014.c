// CCCC_FLAGS: tests/fixtures/dup_tag_1014_impl.c tests/fixtures/dup_tag_1014_private.c -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*struct DyGC1014 \{[\s\S]*int v;)(?=[\s\S]*struct DyGC1014__cccc_dup[0-9]+ \{[\s\S]*double d;)
// CCCC_REJECT_STDOUT: struct DyGC1014 \{[\s\S]*\};\n\nstruct DyGC1014 \{
//
// #1014: two translation units each independently completing a same-named
// but differently-shaped struct tag (the opaque-handle idiom used
// per-backend -- a shared header forward-declares
// `typedef struct DyGC1014 DyGC1014;`, each .c privately supplies its own
// `struct DyGC1014 { ... };`) both serialized verbatim under the identical
// plain tag name in -c=native/-m output, a hard "redefinition of
// 'DyGC1014'" from the host compiler -- even though same_type_or_origin()
// (src/serialize.c) already correctly treats the two shapes as distinct
// types (tag matches, member-wise comparison fails), so collect_type()
// never wrongly deduped them. There was simply no tag-level analogue of
// rename_colliding_static_names() (#1002, which only renames colliding
// Obj/function/variable names, never a struct/union/enum tag).
//
// Fixed with rename_colliding_type_tags() (src/serialize.c): every
// colliding group but one is renamed to `<name>__cccc_dup<N>` (sharing
// #1002's suffix and counter). At most one group can keep the plain
// spelling -- the output still replays the shared header's own `#include`,
// which binds its `DyGC1014` textually, so the "header-exposed" group
// (here, fixtures/dup_tag_1014_impl.c, whose gc_open_1014/gc_val_1014
// signatures the header itself declares) always wins regardless of which
// TU is listed first on the command line; fixtures/dup_tag_1014_private.c
// (never includes the header at all) is always the one renamed. See
// test_serialize_dup_tag_1014_rev.c for the input-order-independence proof
// and test_serialize_dup_tag_1014_pureuse.c for the companion
// find_tag_name() lookup fix this required. Verified this exact program
// printed two identically-named `struct DyGC1014 { ... };` bodies before
// the fix. tools/comptime_native_smoke.py's case is the load-bearing proof
// the resulting native binary actually links and runs, since a dropped/
// colliding struct definition is a host *compile* failure no -m shape
// assertion alone can see.
#include "fixtures/dup_tag_1014.h"

int main(void) {
    DyGC1014 *g = gc_open_1014();
    return gc_val_1014(g);
}
