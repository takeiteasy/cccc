// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'outside_global_894'
// Ticket #894: an uninitialized global declared in a decl-only header (not
// the primary file, not a comptime-defining file) must stay refused by the
// #890 object-splice gate even though the header's own TYPES splice
// freely. Splicing it would let init_macro_globals's zero-filled data
// segment silently stand in for a global the comptime body should never
// have been able to see -- this must be a loud, ordinary "undefined
// variable" error, not a silent 0.
#include "comptime_decl_index_object_gate_894.h"

[[cccc::comptime]]
int use_it(void) {
    return outside_global_894;
}

int main(void) { return use_it(); }
