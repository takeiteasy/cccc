// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: (?=[\s\S]*static const unsigned char my_bytes_1265\[4\];)(?=[\s\S]*long long get_ll_1265\(void\))(?=[\s\S]*unsigned int get_ui_1265\(void\))
//
// Ticket #1265: GetType() resolved single-keyword base types from a small
// table but returned NULL for any multi-word spelling ("unsigned char",
// "long long", "unsigned int", ...). NULL then flowed silently through
// MakeConst/MakeArray/GlobalVar into malformed emitted C with no
// diagnostic. GetType() now resolves standard base-type spellings
// regardless of word order (base_type_from_spelling, src/reflection.c),
// mirroring declspec()'s counter->Type mapping -- including `long long` ->
// ty_llong (spelling preserved for -c=native) and `signed char` -> ty_char.
// The C23 `bool` keyword resolves alongside `_Bool`.
#include @comptime <stdio.h>

[[cccc::comptime]]
void gen_bytes(void) {
    Type *uc = GetType("unsigned char");
    Obj  *v  = GlobalVar("my_bytes_1265", MakeArray(MakeConst(uc), 4));
    unsigned char data[4] = {1, 2, 3, 4};
    GlobalVarSetInitData(v, data, 4);
    GlobalVarSetStatic(v, 1);
    PublishNodeAt(v, SyntheticToken("my_bytes_1265"));
}
gen_bytes();

[[cccc::comptime]]
void gen_ll(void) {
    Obj *fn = MakeFunction("get_ll_1265", GetType("long long"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
}
gen_ll();

[[cccc::comptime]]
void gen_ui(void) {
    Obj *fn = MakeFunction("get_ui_1265", GetType("int unsigned"));
    FunctionSetBody(fn, Quote("return 7;"));
    PublishNode(fn);
}
gen_ui();

[[cccc::comptime]]
void gen_more(void) {
    // Resolve without asserting on their spellings in the output -- these
    // exercise the remaining switch arms.
    if (!GetType("signed char") || !GetType("unsigned long long") ||
        !GetType("long double") || !GetType("bool"))
        MacroErrorAt(0, "multi-word base spelling failed to resolve");
}
gen_more();

int main(void) {
    (void)my_bytes_1265;
    return (int)get_ll_1265() - (int)get_ui_1265() + 7;
}
