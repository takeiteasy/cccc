// Test ticket #229: global macro calls run pre-parse, so generated
// functions are visible everywhere regardless of source order.

[[jcc::macro]]
void generate_late(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("late_generated", int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
}

int main(void) {
    return late_generated();
}

generate_late();
