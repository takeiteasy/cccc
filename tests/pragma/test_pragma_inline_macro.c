// Test: inline pragma macros auto-execute at declaration (ticket #121)
//
// An `inline` pragma macro:
//   - Runs automatically at its declaration point (no explicit call needed)
//   - Generates a forward declaration visible to the parser
//   - Adds the function definition to the program for codegen
//
// Notably absent: manual forward declaration for generated_func,
//                 and any explicit macro call site.

#include <reflection.h>

#pragma macro
inline _Node *generate_const_func(void) {
    _VirtualMachine *vm = __jcc_get_vm();

    _Type *int_type = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, "generated_func", int_type);

    _Node *ret_val = __jcc_ast_int_literal(vm, 42);
    _Node *ret_stmt = __jcc_ast_return(vm, ret_val);
    __jcc_ast_function_set_body(vm, fn, ret_stmt);

    return __jcc_ast_int_literal(vm, 0);
}

int main(void) {
    // No macro call — inline macros run automatically.
    // generated_func was created by the inline macro above.
    int result = generated_func();
    return result - 42;  // 0 on success
}
