// CCCC_FLAGS: -m -G --emit-only
// CCCC_EXPECT_STDOUT: int get_answer\(void\)
// CCCC_REJECT_STDOUT: #include <stddef.h>
// Test: --emit-only suppresses auto-capture; non-annotated directives don't appear.

#include <stddef.h>

#pragma cccc comptime begin

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("get_answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    PublishNode(fn);
}

gen();

#pragma cccc comptime end

int get_answer(void);
int main(void) { return get_answer(); }
