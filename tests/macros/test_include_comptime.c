// Test #include [[cccc::comptime]]: glob.h is visible only during the comptime pass.
// The runtime translation unit never sees glob_t or glob().

#include [[cccc::comptime]] <glob.h>

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
