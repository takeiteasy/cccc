// #1256: comptime code may take the address of an FFI-registered host libc
// function and call it through the pointer. The comptime patcher binds an
// unresolved function-address reference to the FFI dispatch token -- the same
// fallback the runtime patcher (src/codegen_func.c) already has. The
// declaration must reach the comptime pass; an explicit prototype in the
// primary file suffices (so does @comptime / @shared header routing).

unsigned long strlen(const char *s);

[[cccc::comptime]]
void gen(void) {
    unsigned long (*p)(const char *) = strlen;
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral((int)p("comptime!") + 33)));
}
gen();

int main(void) {
    return f(); // strlen("comptime!") == 9; 9 + 33 == 42
}
