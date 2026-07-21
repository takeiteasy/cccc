// Ticket #684: a plain #include "relative.h" (no @comptime/@shared route)
// nested inside an open #pragma cccc comptime begin...end block inherits
// the comptime-only context (ctx_top(vm)->type == CTX_COMPTIME) and must
// also resolve its relative path — same underlying bug as the routed form,
// different code path (src/preprocess.c's PP_INCLUDE switch case, not the
// early @comptime directive-route interception).

#pragma cccc comptime begin
#include "comptime_nested_relative_include.h"
#pragma cccc comptime end

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(comptime_nested_quadruple(10) + 2)));
}

generate_result();

int main(void) {
    return result();
}
