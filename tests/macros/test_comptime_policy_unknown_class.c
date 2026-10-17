// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --comptime-allow=bogus
// CCCC_EXPECT_STDERR: --comptime-allow: unknown class 'bogus'
#include[[cccc::comptime]] <stdio.h>
#include[[cccc::comptime]] <stdlib.h>
#include[[cccc::comptime]] <string.h>

[[cccc::comptime]]
int probe(void) {
    return 42;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));
}

gen();

int main(void) {
    return result();
}
