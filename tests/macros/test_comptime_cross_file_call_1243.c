// #1243: a [[cccc::comptime]] body may call an ordinary C function whose
// definition lives in another command-line input file -- the conventional
// .h/.c split. helper_double is only declared here (via the routed header);
// its body, and the static helper_triple it transitively calls, are
// forwarded into the comptime program on demand.
//
// CCCC_FLAGS: tests/fixtures/comptime_cross_file_1243_helper.c
#include @comptime "tests/fixtures/comptime_cross_file_1243_helper.h"

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(helper_double(21))));
}
gen();

int main(void) {
    return f();
}
