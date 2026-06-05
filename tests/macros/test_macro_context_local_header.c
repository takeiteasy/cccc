#include "macro_context_local.h"

[[jcc::comptime]]
int local_header_type_size(void) {
    macro_context_local_t item;
    item.value = 42;
    return sizeof(item) > 0 && item.value == 42;
}

[[jcc::macro]]
void generate_local_header_context_result(void) {
    _Obj *fn = _AST_FUNCTION("local_header_context_result", _AST_GET_TYPE("int"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(local_header_type_size() ? 42 : 1)));
}

generate_local_header_context_result();

int main(void) {
    return local_header_context_result();
}

