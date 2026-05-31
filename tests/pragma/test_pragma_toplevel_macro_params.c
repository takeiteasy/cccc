// Test ticket #122: file-scope macro call publishes generated parameters.

#include <reflection.h>

#pragma macro
_Node *generate_add(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("generated_add", int_ty);

    _AST_FUNCTION_ADD_PARAM(fn, "a", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "b", int_ty);

    _Node *sum = _AST_BINARY(_ADD, _AST_PARAM_REF(fn, "a"),
                             _AST_PARAM_REF(fn, "b"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(sum));
    _AST_FORWARD_DECLARE(fn);

    return _AST_INT_LITERAL(0);
}

generate_add();

int main(void) {
    return generated_add(20, 22);
}
