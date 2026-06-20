// #include @shared makes header #defines visible to both runtime and comptime.
// COMPTIME_SECRET must be usable inside a comptime function body.
#include @shared "comptime_macro_header.h"

[[cccc::comptime]]
int get_secret(void) {
    return COMPTIME_SECRET; // must be visible via @shared re-include
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(get_secret())));
}

generate_result();

int main(void) {
    // COMPTIME_SECRET also visible at runtime (both contexts get the header).
    int val = COMPTIME_SECRET;
    (void)val;
    return result();
}
