// Test ticket #51: __jcc_ast_while / __jcc_ast_for / __jcc_ast_do_while — execution.
// Generates real functions containing loops via __jcc_ast_function + set_body,
// then calls those functions and checks the output values.
//
// Loop bodies use global state (no intermediate locals needed in the generated
// function) so __jcc_ast_local_var's current_fn-injection is not required here.

// ---- Globals modified by the generated loops ----
int g_while_result;
int g_for_result;
int g_do_result;

// Macro: generates a function that uses a while loop to compute 0+1+2+3+4 = 10
// into g_while_result. Loop: while (g_while_result < 5) g_while_result++
// but accumulates: uses a second global for the accumulator approach:
//   g_while_result = 0; i = 1..5; g_while_result += i
// Since we can't easily use a loop-local variable for `i`, we use a simpler
// approach: g_while_result is both the counter and the accumulator.
// Generated: while (g_while_result < 10) g_while_result = g_while_result + 1;
[[jcc::macro(inline)]]
_Node *gen_while_func() {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *void_ty = __jcc_ast_get_type(vm, "void");
    _Obj *fn = __jcc_ast_function(vm, "fill_while", void_ty);

    _Type *int_ty = __jcc_ast_get_type(vm, "int");
    (void)int_ty;

    // cond: g_while_result < 10
    _Node *lhs_c  = __jcc_ast_var_ref(vm, "g_while_result");
    _Node *ten    = __jcc_ast_int_literal(vm, 10);
    _Node *cond   = __jcc_ast_binary(vm, _LT, lhs_c, ten);

    // body: g_while_result = g_while_result + 1
    _Node *lhs_a  = __jcc_ast_var_ref(vm, "g_while_result");
    _Node *lhs_a2 = __jcc_ast_var_ref(vm, "g_while_result");
    _Node *one    = __jcc_ast_int_literal(vm, 1);
    _Node *add    = __jcc_ast_binary(vm, _ADD, lhs_a2, one);
    _Node *asgn   = __jcc_ast_assign(vm, lhs_a, add);
    _Node *body   = __jcc_ast_expr_stmt(vm, asgn);

    _Node *loop = __jcc_ast_while(vm, cond, body);
    __jcc_ast_function_set_body(vm, fn, loop);
    return __jcc_ast_int_literal(vm, 0);
}

// Macro: generates a function that uses a for loop.
// Generated: for (; g_for_result < 5; ) g_for_result = g_for_result + 1;
[[jcc::macro(inline)]]
_Node *gen_for_func() {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *void_ty = __jcc_ast_get_type(vm, "void");
    _Obj *fn = __jcc_ast_function(vm, "fill_for", void_ty);

    _Node *lhs_c  = __jcc_ast_var_ref(vm, "g_for_result");
    _Node *five   = __jcc_ast_int_literal(vm, 5);
    _Node *cond   = __jcc_ast_binary(vm, _LT, lhs_c, five);

    _Node *lhs_a  = __jcc_ast_var_ref(vm, "g_for_result");
    _Node *lhs_a2 = __jcc_ast_var_ref(vm, "g_for_result");
    _Node *one    = __jcc_ast_int_literal(vm, 1);
    _Node *add    = __jcc_ast_binary(vm, _ADD, lhs_a2, one);
    _Node *asgn   = __jcc_ast_assign(vm, lhs_a, add);
    _Node *body   = __jcc_ast_expr_stmt(vm, asgn);

    // for(NULL, cond, NULL, body) — init and inc are NULL
    _Node *loop = __jcc_ast_for(vm, (void*)0, cond, (void*)0, body);
    __jcc_ast_function_set_body(vm, fn, loop);
    return __jcc_ast_int_literal(vm, 0);
}

// Macro: generates a function that uses a do-while loop.
// Generated: do { g_do_result = g_do_result + 1; } while (g_do_result < 3);
// After: g_do_result == 3 (started at 0, runs 3 times)
[[jcc::macro(inline)]]
_Node *gen_do_func() {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *void_ty = __jcc_ast_get_type(vm, "void");
    _Obj *fn = __jcc_ast_function(vm, "fill_do", void_ty);

    _Node *lhs_b  = __jcc_ast_var_ref(vm, "g_do_result");
    _Node *lhs_b2 = __jcc_ast_var_ref(vm, "g_do_result");
    _Node *one    = __jcc_ast_int_literal(vm, 1);
    _Node *add    = __jcc_ast_binary(vm, _ADD, lhs_b2, one);
    _Node *asgn   = __jcc_ast_assign(vm, lhs_b, add);
    _Node *body   = __jcc_ast_expr_stmt(vm, asgn);

    _Node *lhs_c  = __jcc_ast_var_ref(vm, "g_do_result");
    _Node *three  = __jcc_ast_int_literal(vm, 3);
    _Node *cond   = __jcc_ast_binary(vm, _LT, lhs_c, three);

    _Node *loop = __jcc_ast_do_while(vm, body, cond);
    __jcc_ast_function_set_body(vm, fn, loop);
    return __jcc_ast_int_literal(vm, 0);
}

void fill_while(void);
void fill_for(void);
void fill_do(void);

int main(void) {
    // Trigger macro generation (side-effect: functions are created)
    int _w = gen_while_func();
    int _f = gen_for_func();
    int _d = gen_do_func();
    (void)_w; (void)_f; (void)_d;

    // While loop: g_while_result starts at 0, loop counts it to 10
    g_while_result = 0;
    fill_while();
    if (g_while_result != 10) return 1;

    // For loop: g_for_result starts at 0, loop counts it to 5
    g_for_result = 0;
    fill_for();
    if (g_for_result != 5) return 2;

    // Do-while: g_do_result starts at 0, increments while < 3 → stops at 3
    g_do_result = 0;
    fill_do();
    if (g_do_result != 3) return 3;

    return 42;
}
