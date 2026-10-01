// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: only _Pragma\("omp \.\.\."\) is supported in a Quote
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, Quote("_Pragma(\"once\") return 1;"));
}
gen();

int main(void) {
    return f();
}
