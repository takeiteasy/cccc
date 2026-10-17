// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comptime call to 'getenv' denied \(class env\); pass --comptime-allow=env
#include[[cccc::comptime]] <stdio.h>
#include[[cccc::comptime]] <stdlib.h>
#include[[cccc::comptime]] <string.h>

[[cccc::comptime]]
int probe(void) {
    return getenv("PATH") ? 42 : 42;
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
