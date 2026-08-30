// Test that a [[cccc::comptime]] function may return a struct/union by
// value, and that a struct-returning call in a comptime body works too. The
// RETBUF pool backing by-value aggregate returns must be allocated for the
// comptime pass, not just for gen()/cc_repl_compile_new().

typedef struct { int a, b; } S;

[[cccc::comptime]]
S mk(int x) {
    S s;
    s.a = x;
    s.b = x + 1;
    return s;
}

[[cccc::comptime]]
void generate_result(void) {
    S s = mk(3);
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(
                             (s.a == 3 && s.b == 4) ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
