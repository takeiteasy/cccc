// CCCC_FLAGS: tests/fixtures/dandy_pattern_999_a.c
// A static const vtable of pointers to later-defined file statics, plus a
// guard-less shared header with a static inline accessor reached from two TUs,
// links under -c=native.
#include "fixtures/dandy_pattern_999.h"
int dp_call_a(void);
DPValue dp_helper(DPValue v) {
    return dp_box(v);
}
int main(void) {
    return dp_call_a() + (dp_helper(21) == 21 ? 0 : 999);
}
