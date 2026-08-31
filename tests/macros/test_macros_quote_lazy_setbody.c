// Ticket #1242: a QuoteLazy() result passed directly to FunctionSetBody
// (rather than spliced via a $N placeholder into another template) is
// materialised there too -- see the ND_QUOTE_LAZY check at the top of
// __builtin_ast_function_set_body (src/reflection.c).

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, QuoteLazy("return 42;"));
    }
}
gen();

int main(void) {
    return f();
}
