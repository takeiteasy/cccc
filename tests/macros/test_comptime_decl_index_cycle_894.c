// Ticket #894: mutually recursive struct tags across an unrouted,
// decl-only header. Splicing "A894" (triggered by find_tag()'s hook) must
// itself trigger a nested splice of "B894" (a pointer member), which loops
// back to "A894" -- the CD_IN_PROGRESS cycle guard must let this terminate
// rather than recursing forever.
#include "comptime_decl_index_cycle_894.h"

[[cccc::comptime]]
int use_cycle(void) {
    struct B894 b;
    b.a = 0;
    struct A894 a;
    a.val = 42;
    a.b   = 0;
    return a.val;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(use_cycle())));
}
gen();

int main(void) {
    return result();
}
