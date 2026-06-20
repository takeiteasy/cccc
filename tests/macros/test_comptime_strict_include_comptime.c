// CCCC_FLAGS: --strict-comptime-includes
// With --strict-comptime-includes, #include @comptime still works.

#include @comptime <glob.h>

[[cccc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(glob_type_size() ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
