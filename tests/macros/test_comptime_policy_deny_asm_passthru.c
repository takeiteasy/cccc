// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: comptime call to '__cccc_asm_passthru_[0-9]+' denied \(not in any capability class\); pass --comptime-allow=all

[[cccc::comptime]]
int probe(void) {
    asm volatile("");
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
