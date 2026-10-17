// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comptime call to 'dlopen' denied \(class dlopen\); pass --comptime-allow=dlopen

#include[[cccc::comptime]] <dlfcn.h>

[[cccc::comptime]]
int probe(void) {
    void *h = dlopen(0, RTLD_LAZY);
    return h ? 42 : 42;
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
