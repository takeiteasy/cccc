// Ticket #627 migration example: primary-file #define macros are not directly
// visible in comptime function bodies. Instead, place the definition in a
// header and include it with @shared so it is visible in both the runtime and
// comptime contexts.
#include @shared "comptime_primary_answer.h"

[[cccc::comptime]]
int get_answer(void) {
    return PRIMARY_ANSWER; // visible because the header was @shared-included
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(get_answer())));
}

generate_result();

int main(void) {
    return result();
}
