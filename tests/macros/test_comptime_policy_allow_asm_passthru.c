// CCCC_FLAGS: --asm-passthru --comptime-allow=all

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
