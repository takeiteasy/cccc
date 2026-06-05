#include <glob.h>

[[jcc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[jcc::macro]]
void generate_glob_context_result(void) {
    _Obj *fn = _AST_FUNCTION("glob_context_result", _AST_GET_TYPE("int"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(glob_type_size() ? 42 : 1)));
}

generate_glob_context_result();

int main(void) {
    return glob_context_result();
}

