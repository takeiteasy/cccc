// #1243: the body sweep is driven by bodyless function Objs, so it also
// covers a function whose address is taken in comptime code (a
// func_addr_patch) rather than called directly. helper_double is defined in
// a separate input file; the comptime body calls it through a pointer.
//
// CCCC_FLAGS: tests/fixtures/comptime_cross_file_1243_helper.c

[[cccc::comptime]]
void gen(void) {
    int (*p)(int) = helper_double;
    Obj *fn       = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(p(21))));
}
gen();

int main(void) {
    return f();
}
