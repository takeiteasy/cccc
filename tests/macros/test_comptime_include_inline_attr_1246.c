// Test #1246: a routed comptime include followed by a [[cccc::comptime]]
// definition written entirely on one line must not desync the synthesized
// comptime token stream's line boundaries (previously walked off the end of
// the token list and segfaulted in skip_line()).

#include @comptime <glob.h>
[[cccc::comptime]] int glob_type_size(void) { glob_t g; return sizeof(g) > 0; }
[[cccc::comptime]] void generate_result(void) { Obj *fn = MakeFunction("result", GetType("int")); FunctionSetBody(fn, MakeReturn(MakeIntLiteral(glob_type_size() ? 42 : 1))); }

generate_result();

int main(void) {
    return result();
}
