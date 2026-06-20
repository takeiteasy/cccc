// CCCC_FLAGS: --comptime-include-all
// With --comptime-include-all, #define macros from plain #includes are
// forwarded to comptime (legacy all-headers behavior). COMPTIME_SECRET must
// be usable inside a comptime function body.
#include "comptime_macro_header.h"

[[cccc::comptime]]
int get_secret(void) {
    return COMPTIME_SECRET; // visible under --comptime-include-all
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
