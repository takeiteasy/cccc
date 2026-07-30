// Test ticket #51: __builtin_ast_while / __builtin_ast_for / __builtin_ast_do_while — execution.
// Generates real functions containing loops via __builtin_ast_function + set_body,
// then calls those functions and checks the output values.
//
// Loop bodies use global state (no intermediate locals needed in the generated
// function) so __builtin_ast_local_var's current_fn-injection is not required here.

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
[[cccc::comptime]]
Node *gen_while_func() {
    Type *void_ty = __builtin_ast_get_type("void");
    Obj *fn = __builtin_ast_function("fill_while", void_ty);

    Type *int_ty = __builtin_ast_get_type("int");
    (void)int_ty;

    // cond: g_while_result < 10
    Node *lhs_c  = __builtin_ast_var_ref("g_while_result");
    Node *ten    = __builtin_ast_int_literal(10);
    Node *cond   = __builtin_ast_binary(NK_LT, lhs_c, ten);

    // body: g_while_result = g_while_result + 1
    Node *lhs_a  = __builtin_ast_var_ref("g_while_result");
    Node *lhs_a2 = __builtin_ast_var_ref("g_while_result");
    Node *one    = __builtin_ast_int_literal(1);
    Node *add    = __builtin_ast_binary(NK_ADD, lhs_a2, one);
    Node *asgn   = __builtin_ast_assign(lhs_a, add);
    Node *body   = __builtin_ast_expr_stmt(asgn);

    Node *loop = __builtin_ast_while(cond, body);
    __builtin_ast_function_set_body(fn, loop);
    return __builtin_ast_int_literal(0);
}

// Macro: generates a function that uses a for loop.
// Generated: for (; g_for_result < 5; ) g_for_result = g_for_result + 1;
[[cccc::comptime]]
Node *gen_for_func() {
    Type *void_ty = __builtin_ast_get_type("void");
    Obj *fn = __builtin_ast_function("fill_for", void_ty);

    Node *lhs_c  = __builtin_ast_var_ref("g_for_result");
    Node *five   = __builtin_ast_int_literal(5);
    Node *cond   = __builtin_ast_binary(NK_LT, lhs_c, five);

    Node *lhs_a  = __builtin_ast_var_ref("g_for_result");
    Node *lhs_a2 = __builtin_ast_var_ref("g_for_result");
    Node *one    = __builtin_ast_int_literal(1);
    Node *add    = __builtin_ast_binary(NK_ADD, lhs_a2, one);
    Node *asgn   = __builtin_ast_assign(lhs_a, add);
    Node *body   = __builtin_ast_expr_stmt(asgn);

    // for(NULL, cond, NULL, body) — init and inc are NULL
    Node *loop = __builtin_ast_for((void*)0, cond, (void*)0, body);
    __builtin_ast_function_set_body(fn, loop);
    return __builtin_ast_int_literal(0);
}

// Macro: generates a function that uses a do-while loop.
// Generated: do { g_do_result = g_do_result + 1; } while (g_do_result < 3);
// After: g_do_result == 3 (started at 0, runs 3 times)
[[cccc::comptime]]
Node *gen_do_func() {
    Type *void_ty = __builtin_ast_get_type("void");
    Obj *fn = __builtin_ast_function("fill_do", void_ty);

    Node *lhs_b  = __builtin_ast_var_ref("g_do_result");
    Node *lhs_b2 = __builtin_ast_var_ref("g_do_result");
    Node *one    = __builtin_ast_int_literal(1);
    Node *add    = __builtin_ast_binary(NK_ADD, lhs_b2, one);
    Node *asgn   = __builtin_ast_assign(lhs_b, add);
    Node *body   = __builtin_ast_expr_stmt(asgn);

    Node *lhs_c  = __builtin_ast_var_ref("g_do_result");
    Node *three  = __builtin_ast_int_literal(3);
    Node *cond   = __builtin_ast_binary(NK_LT, lhs_c, three);

    Node *loop = __builtin_ast_do_while(body, cond);
    __builtin_ast_function_set_body(fn, loop);
    return __builtin_ast_int_literal(0);
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
