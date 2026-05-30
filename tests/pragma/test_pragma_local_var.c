// Test ticket #77: jcc_ast_local_var / jcc_ast_local_var_unique

#include <reflection.h>

// Macro that creates a hygienic temporary, assigns arg*2 to it, and returns it.
// Demonstrates that the gensym'd name doesn't collide with user locals.
#pragma macro
JCC_Node *doubled(JCC_Node *arg) {
    JCC *vm = jcc_get_vm();

    // Inject a unique (gensym'd) local of type int
    JCC_Type *ty_int = jcc_ast_get_type(vm, "int");
    JCC_Node *tmp = jcc_ast_local_var_unique(vm, ty_int);

    // Build: tmp = arg * 2
    JCC_Node *two   = jcc_ast_int_literal(vm, 2);
    JCC_Node *mul   = jcc_ast_binary(vm, JCC_ND_MUL, arg, two);
    JCC_Node *asgn  = jcc_ast_assign(vm, tmp, mul);

    // Return tmp (the assignment expression already set tmp, but return tmp ref
    // as an ND_ASSIGN evaluates to the assigned value — return the assign expr)
    return asgn;
}

// Macro that creates a named local and returns a reference to it.
// Proves jcc_ast_local_var works (not just the unique variant).
#pragma macro
JCC_Node *make_local_forty_two(void) {
    JCC *vm = jcc_get_vm();
    JCC_Type *ty_int = jcc_ast_get_type(vm, "int");
    JCC_Node *var  = jcc_ast_local_var(vm, "named_tmp", ty_int);
    JCC_Node *val  = jcc_ast_int_literal(vm, 42);
    return jcc_ast_assign(vm, var, val);
}

int main(void) {
    // doubled: unique temp should not capture any existing local
    int result = doubled(7);    // tmp = 7*2 = 14 (assign expr = 14)
    if (result != 14) return 1;

    int result2 = doubled(10);  // tmp = 10*2 = 20
    if (result2 != 20) return 2;

    // named local
    int v = make_local_forty_two();  // named_tmp = 42
    if (v != 42) return 3;

    return 42;
}
