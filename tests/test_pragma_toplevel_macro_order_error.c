// EXPECT_COMPILE_ERROR
// Test ticket #122: generated functions are visible only after publication.

#include <reflection.h>

#pragma macro
_Node *generate_late(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("late_generated", int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}

int main(void) {
    return late_generated();
}

generate_late();
