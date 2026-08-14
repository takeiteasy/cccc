// CCCC_FLAGS: tests/fixtures/tu_isolation_1001_guard_a.c
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
//
// #1001: an include-guard macro tripped by the *first* TU to #include a
// header used to silently empty that same #include for every later TU
// sharing this cccc invocation's single VirtualMachine, even though each
// is a separate translation unit that should get its own complete copy.
// This was masked by a second, load-bearing bug (parse()'s file scope was
// never left between TUs, documented at struct Compiler's primary_file
// comment) -- the guard-emptied #include produced nothing new, but the
// earlier TU's own declarations were still reachable through the leaked
// scope chain, so this exact shape (TU2 using a type/inline function from
// a guarded header TU1 already included) happened to still work by
// accident. Both bugs are fixed together (cc_reset_preprocessor_state_
// for_next_tu + cc_leave_top_file_scope, preprocess.c/parse.c): TU2 now
// gets a genuinely independent, fully re-expanded copy of the header
// instead of relying on TU1's leaked scope.
#include "fixtures/tu_isolation_1001_guard.h"

int tu_isolation_1001_guard_a(void);

int main(void) {
    return tu_isolation_1001_guard_a() + (int)tu_isolation_1001_box(22);
}
