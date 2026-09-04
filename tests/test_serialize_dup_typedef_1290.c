// CCCC_FLAGS: tests/fixtures/dup_typedef_1290_a.c -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*#include "dup_typedef_1290\.h")(?=[\s\S]*int dup_typedef_1290_sum\(DupTypedef1290 v\))
// CCCC_REJECT_STDOUT: \} DupTypedef1290;
//
// #1290: this TU (input_files[1], non-primary -- fixtures/dup_typedef_1290_a.c
// is input_files[0]/the primary file, listed first via CCCC_FLAGS) never
// includes fixtures/dup_typedef_1290.h at all -- it independently declares
// its own tagless `typedef struct { int a; int b; } DupTypedef1290;` with
// the identical name and shape, exactly src/json.c's own TypeVec next to
// src/serialize_internal.h's in the real 82-file corpus this ticket's
// self-hosting spike (#1132) hit it on.
//
// ctx->typedefs is a whole-program registry (vm->compiler.type_names is one
// VM-wide list, never reset between per-TU parses -- only the preprocessor's
// #define/#pragma once bookkeeping is reset per #1001), so both this TU's
// own record (from_include=false, it's the file's own declaration) and the
// header's record (from_include=true, reached via
// fixtures/dup_typedef_1290_a.c's #include) land in that one list. Two
// independent code paths can resolve a tagless aggregate's definition to
// whichever record happens to match first
// -- serialize_type_defs_for_owner's ctx->defs loop (gated by
// type_def_is_from_include_suppressed) and its separate ctx->typedefs loop
// via emit_typedef_and_deps (gated by typedef_alias_header_suppressed) --
// and previously neither one checked whether some OTHER same-named record
// already marked the type header-supplied: this TU's own from_include=false
// record won either gate and printed the struct body again, right next to
// the replayed `#include "dup_typedef_1290.h"`, a host "typedef redefinition
// with different types" no VM-mode run of this program would ever surface.
//
// Fixed by typedef_name_is_header_supplied() (src/serialize_type.c), shared
// by both gates: a tagless record whose OWN from_include is false is still
// treated as header-supplied when some other whole-program record of the
// exact same name is from_include (and not always_emit). Matched by name,
// deliberately not by structure -- a structural match would misfire the
// same way find_generated_uncaptured_typedef()'s own #1168/#1169 comment
// warns against (a same-representation scalar typedef structurally matching
// an unrelated header typedef). tools/comptime_native_smoke.py's
// case_dup_typedef_1290_native_round_trip is the load-bearing proof the
// resulting native binary actually links and runs, since a duplicated
// struct definition is a host *compile* failure no -m shape assertion
// alone can see.
typedef struct {
    int a;
    int b;
} DupTypedef1290;

static int dup_typedef_1290_local_use(void) {
    DupTypedef1290 v;
    v.a = 19;
    v.b = 23;
    return v.a + v.b;
}

int main(void) {
    return dup_typedef_1290_local_use();
}
