// Ticket #894: a comptime body referencing "Outer894" must transitively
// pull in Mid894 and Inner894, none of which are visible up front -- each
// resolves only when the previous splice's own body asks for the next name.
#include "comptime_decl_index_transitive_b_894.h"

[[cccc::comptime]]
int use_outer(void) {
    Outer894 o = 42;
    return o;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(use_outer())));
}
gen();

int main(void) {
    return result();
}
