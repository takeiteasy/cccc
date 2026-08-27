// CCCC_FLAGS: tests/fixtures/opaque_handle_1010_use.c -m
// CCCC_EXPECT_STDOUT: struct DyAtoms1010 \{[\s\S]*int x;[\s\S]*\};
// CCCC_REJECT_STDOUT: struct DyAtoms1010;\n|unsupported expr kind
//
// #1010 defect B: the reverse ordering of test_serialize_opaque_handle_1010.c
// -- the use-site TU (this test's fixture, fixtures/opaque_handle_1010_use.c,
// input_files[0]) is parsed *before* the TU that completes the struct (this
// file, input_files[1]). collect_type() (src/serialize.c) dedups by
// same_type_or_origin(), which treats a tagged incomplete aggregate as
// equal to the tagged complete one (#892) -- so whichever Type* is walked
// first claims ctx->seen/ctx->defs' slot. With the use-site TU walked
// first, the member-less incomplete Type* won the slot and
// serialize_struct_def emitted a bare `struct DyAtoms1010;` instead of the
// real body, even though defect A's fix (TypeNameRecord.defines_type) had
// already picked the right *provenance* record. Fixed by having
// collect_type() swap in a complete Type* over an incomplete one already
// occupying the slot (remove-then-recollect, not in-place mutation, so a
// member type the incomplete stub never referenced is still collected
// ahead of its user). Verified this exact program printed a bare
// `struct DyAtoms1010;` (no members) before the fix.
// tools/comptime_native_smoke.py's case is the load-bearing proof the
// resulting native binary actually links and runs.
#include "fixtures/opaque_handle_1010.h"

struct DyAtoms1010 {
    int x;
};

DyAtoms1010 *make_atoms_1010(void) {
    static struct DyAtoms1010 a;
    a.x = 42;
    return &a;
}

int get_x_1010(DyAtoms1010 *t) {
    return t->x;
}

int opaque_handle_1010_use(void);

int main(void) {
    return opaque_handle_1010_use();
}
