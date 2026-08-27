// CCCC_FLAGS: tests/fixtures/opaque_handle_1010_def.c -m
// CCCC_EXPECT_STDOUT: struct DyAtoms1010 \{[\s\S]*int x;[\s\S]*\};
// CCCC_REJECT_STDOUT: struct DyAtoms1010;\n|unsupported expr kind
//
// #1010 defect A: the opaque-handle idiom (a header forward-declares
// `typedef struct DyAtoms1010 DyAtoms1010;`, exactly one .c file supplies
// `struct DyAtoms1010 { ... };`, other .c files only ever see the
// incomplete typedef) worked single-TU but dropped the struct's definition
// from -c=native/-m output entirely when the completing TU (this test's
// fixture, fixtures/opaque_handle_1010_def.c, input_files[0]) was parsed
// *before* a use-site TU (this file, input_files[1]) that dereferences the
// handle. Root cause: record_type_name() prepends, so with more than one
// command-line input file a later TU's own header forward declaration
// could end up scanned ahead of the earlier TU's completing record, and
// same_type_or_origin() deliberately treats a tagged incomplete aggregate
// as equal to the tagged complete one (#892) -- so the forward
// declaration's from_include=true record won, wrongly suppressing the only
// definition available. Fixed via TypeNameRecord.defines_type +
// find_tag_name_for_provenance() (src/serialize.c). Verified this exact
// program printed no `struct DyAtoms1010` definition anywhere before the
// fix. tools/comptime_native_smoke.py's case is the load-bearing proof the
// resulting native binary actually links and runs.
#include "fixtures/opaque_handle_1010.h"

int main(void) {
    DyAtoms1010 *t = make_atoms_1010();
    return get_x_1010(t);
}
