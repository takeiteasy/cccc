// EXPECT_COMPILE_ERROR
// #define macros from a plain (runtime-only) #include must NOT be visible
// to the comptime pass. COMPTIME_SECRET from comptime_macro_header.h must
// be an undefined identifier inside the comptime function body.
#include "comptime_macro_header.h"

[[cccc::comptime]]
int get_secret(void) {
    return COMPTIME_SECRET; // must be undefined — gated by runtime-only scoping
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(get_secret())));
}

generate_result();

int main(void) {
    return result();
}
