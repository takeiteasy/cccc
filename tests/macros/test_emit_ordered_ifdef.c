// CCCC_FLAGS: -M -G
// CCCC_EXPECT_STDOUT: #ifdef _WIN32.*int generated_answer\(void\);.*#endif
// CCCC_REJECT_STDOUT: #endif.*int generated_answer\(void\);
#include @emit <stddef.h>

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("generated_answer", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
}

#ifdef @emit _WIN32
gen();
#endif @emit

int main(void) { return 42; }
