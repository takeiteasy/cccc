// Test ticket #51: jcc_ast_while / jcc_ast_for / jcc_ast_do_while — execution.
// Generates real functions containing loops via jcc_ast_function + set_body,
// then calls those functions and checks the output values.
//
// Loop bodies use global state (no intermediate locals needed in the generated
// function) so jcc_ast_local_var's current_fn-injection is not required here.

#include <reflection.h>

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
#pragma macro
JCC_Node *gen_while_func() {
    JCC *vm = jcc_get_vm();
    JCC_Type *void_ty = jcc_ast_get_type(vm, "void");
    JCC_Obj *fn = jcc_ast_function(vm, "fill_while", void_ty);

    JCC_Type *int_ty = jcc_ast_get_type(vm, "int");
    (void)int_ty;

    // cond: g_while_result < 10
    JCC_Node *lhs_c  = jcc_ast_var_ref(vm, "g_while_result");
    JCC_Node *ten    = jcc_ast_int_literal(vm, 10);
    JCC_Node *cond   = jcc_ast_binary(vm, JCC_ND_LT, lhs_c, ten);

    // body: g_while_result = g_while_result + 1
    JCC_Node *lhs_a  = jcc_ast_var_ref(vm, "g_while_result");
    JCC_Node *lhs_a2 = jcc_ast_var_ref(vm, "g_while_result");
    JCC_Node *one    = jcc_ast_int_literal(vm, 1);
    JCC_Node *add    = jcc_ast_binary(vm, JCC_ND_ADD, lhs_a2, one);
    JCC_Node *asgn   = jcc_ast_assign(vm, lhs_a, add);
    JCC_Node *body   = jcc_ast_expr_stmt(vm, asgn);

    JCC_Node *loop = jcc_ast_while(vm, cond, body);
    jcc_ast_function_set_body(vm, fn, loop);
    return jcc_ast_int_literal(vm, 0);
}

// Macro: generates a function that uses a for loop.
// Generated: for (; g_for_result < 5; ) g_for_result = g_for_result + 1;
#pragma macro
JCC_Node *gen_for_func() {
    JCC *vm = jcc_get_vm();
    JCC_Type *void_ty = jcc_ast_get_type(vm, "void");
    JCC_Obj *fn = jcc_ast_function(vm, "fill_for", void_ty);

    JCC_Node *lhs_c  = jcc_ast_var_ref(vm, "g_for_result");
    JCC_Node *five   = jcc_ast_int_literal(vm, 5);
    JCC_Node *cond   = jcc_ast_binary(vm, JCC_ND_LT, lhs_c, five);

    JCC_Node *lhs_a  = jcc_ast_var_ref(vm, "g_for_result");
    JCC_Node *lhs_a2 = jcc_ast_var_ref(vm, "g_for_result");
    JCC_Node *one    = jcc_ast_int_literal(vm, 1);
    JCC_Node *add    = jcc_ast_binary(vm, JCC_ND_ADD, lhs_a2, one);
    JCC_Node *asgn   = jcc_ast_assign(vm, lhs_a, add);
    JCC_Node *body   = jcc_ast_expr_stmt(vm, asgn);

    // for(NULL, cond, NULL, body) — init and inc are NULL
    JCC_Node *loop = jcc_ast_for(vm, (void*)0, cond, (void*)0, body);
    jcc_ast_function_set_body(vm, fn, loop);
    return jcc_ast_int_literal(vm, 0);
}

// Macro: generates a function that uses a do-while loop.
// Generated: do { g_do_result = g_do_result + 1; } while (g_do_result < 3);
// After: g_do_result == 3 (started at 0, runs 3 times)
#pragma macro
JCC_Node *gen_do_func() {
    JCC *vm = jcc_get_vm();
    JCC_Type *void_ty = jcc_ast_get_type(vm, "void");
    JCC_Obj *fn = jcc_ast_function(vm, "fill_do", void_ty);

    JCC_Node *lhs_b  = jcc_ast_var_ref(vm, "g_do_result");
    JCC_Node *lhs_b2 = jcc_ast_var_ref(vm, "g_do_result");
    JCC_Node *one    = jcc_ast_int_literal(vm, 1);
    JCC_Node *add    = jcc_ast_binary(vm, JCC_ND_ADD, lhs_b2, one);
    JCC_Node *asgn   = jcc_ast_assign(vm, lhs_b, add);
    JCC_Node *body   = jcc_ast_expr_stmt(vm, asgn);

    JCC_Node *lhs_c  = jcc_ast_var_ref(vm, "g_do_result");
    JCC_Node *three  = jcc_ast_int_literal(vm, 3);
    JCC_Node *cond   = jcc_ast_binary(vm, JCC_ND_LT, lhs_c, three);

    JCC_Node *loop = jcc_ast_do_while(vm, body, cond);
    jcc_ast_function_set_body(vm, fn, loop);
    return jcc_ast_int_literal(vm, 0);
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
