// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comptime call to 'fopen' denied \(class fs\); pass --comptime-allow=fs
#include[[cccc::comptime]] <stdio.h>
#include[[cccc::comptime]] <stdlib.h>
#include[[cccc::comptime]] <string.h>

[[cccc::comptime]]
int probe(void) {
    FILE *f = fopen("/dev/null", "r");
    if (f)
        fclose(f);
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
