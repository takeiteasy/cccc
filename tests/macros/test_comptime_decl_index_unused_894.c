// Ticket #894: a header carries several declarations a comptime body never
// names -- a struct, an enum, a function prototype, and an object with a
// dependent initializer. Only "UsedOnly894" is ever referenced. None of the
// unused declarations should be examined or spliced; this must compile
// clean.
#include "comptime_decl_index_unused_894.h"

[[cccc::comptime]]
int use_one(void) {
    UsedOnly894 v = 42;
    return v;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(use_one())));
}
gen();

int main(void) {
    return result();
}
