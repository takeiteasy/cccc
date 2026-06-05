// Test #include_comptime: glob.h is visible only during the comptime pass.
// The runtime translation unit never sees glob_t or glob().
// Mirrors test_macro_context_glob.c but uses #include_comptime instead of #include.

#include_comptime <glob.h>

[[jcc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[jcc::macro]]
void generate_result(void) {
    _Obj *fn = _AST_FUNCTION("result", _AST_GET_TYPE("int"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(glob_type_size() ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
