// #1256: a comptime static function-pointer table whose entries are FFI-
// registered host functions resolves through apply_macro_global_relocations,
// which now stores the FFI dispatch token instead of hard-erroring
// ("unsupported macro relocation to undefined function"). Mirrors the runtime
// static-initializer path (apply_global_relocations, src/codegen_regalloc.c).

unsigned long strlen(const char *s);

[[cccc::comptime]]
int probe(void) {
    static unsigned long (*const tbl[])(const char *) = {strlen};
    return (int)tbl[0]("cccc"); // 4
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe() * 10 + 2)));
}
gen();

int main(void) {
    return f(); // 4 * 10 + 2 == 42
}
