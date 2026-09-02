// #1257: the body-forwarding sweep pulls a deep transitive call chain into
// the comptime program one helper per fixed-point round. Only deep_chain_1 is
// named by comptime code; deep_chain_2..40 are forwarded transitively, one
// helper per fixed-point round (each spliced body reveals the next reference).
// This exercises many rounds of the sweep and guards the cross-round memo rewrite
// -- a prototype that gains a body must keep being re-scanned for its own
// callees, or the chain stops being forwarded partway through.
//
// CCCC_FLAGS: tests/fixtures/comptime_deep_chain_1257.c
#include @comptime "tests/fixtures/comptime_deep_chain_1257.h"

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(deep_chain_1(2))));
}
gen();

int main(void) {
    return f(); // 2 + 40 == 42
}
