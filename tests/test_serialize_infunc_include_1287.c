// Ticket #1287: an `#include` written *inside a function body* (not a
// header, a bare statement fragment spliced in textually) was hoisted to
// file scope AND duplicated under -c=native / -c=generated. The
// preprocessor's auto-capture gate (preprocess2(), src/preprocess.c) only
// ever tested routing/mode/file identity, never lexical position, so the
// directive line was captured for verbatim file-scope replay exactly like
// a real top-level `#include <header.h>` -- even though the `.inc`
// fragment's own tokens were separately (and correctly) parsed into the
// enclosing function and serialized in place there. The file-scope copy
// is not even a legal top-level declaration on its own, so a real host
// compiler rejected it outright. Reproduced directly against
// src/macros.c's `register_reflection_ffi()`, which does exactly this
// with `reflection_ffi_register.inc`.
//
// Fixed by tracking brace depth over ordinary tokens in preprocess2() and
// excluding a PP_INCLUDE seen at depth > 0 from the auto-capture gate.
//
// This file carries no header directives of its own: the VM path never
// touches src/serialize*.c or the auto-capture gate, so only the ambient
// -c=native round-trip suite (#1157, on by default) actually exercises
// this fix end-to-end against a real host compiler.
static int accumulate_1287(int n) {
    int acc = 0;
#include "fixtures/infunc_include_1287.inc"
    return acc;
}

int main(void) {
    // acc = 0; acc += 14; acc += 28  ->  42
    return accumulate_1287(14);
}
