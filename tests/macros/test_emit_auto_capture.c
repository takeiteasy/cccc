// CCCC_FLAGS: -M -G
// CCCC_EXPECT_STDOUT: #include <stddef.h>
// Test: directives outside comptime are auto-captured into -G output.

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
