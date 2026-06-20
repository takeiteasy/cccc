// EXPECT_COMPILE_ERROR
// Regular #include declarations are NOT forwarded to comptime by default.
// glob_t from <glob.h> must be unavailable inside the comptime function
// unless the include is annotated @shared or @comptime.

#include <glob.h>

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
