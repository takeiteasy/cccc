// Test ticket #77: __cccc_ast_local_var / __cccc_ast_local_var_unique

// Macro that creates a hygienic temporary, assigns arg*2 to it, and returns it.
// Demonstrates that the gensym'd name doesn't collide with user locals.
[[cccc::comptime(inline)]]
$node_t *doubled($node_t *arg) {
    $vm_t *vm = __cccc_get_vm();

    // Inject a unique (gensym'd) local of type int
    $type_t *ty_int = __cccc_ast_get_type(vm, "int");
    $node_t *tmp = __cccc_ast_local_var_unique(vm, ty_int);

    // Build: tmp = arg * 2
    $node_t *two   = __cccc_ast_int_literal(vm, 2);
    $node_t *mul   = __cccc_ast_binary(vm, nk_mul, arg, two);
    $node_t *asgn  = __cccc_ast_assign(vm, tmp, mul);

    // Return tmp (the assignment expression already set tmp, but return tmp ref
    // as an ND_ASSIGN evaluates to the assigned value — return the assign expr)
    return asgn;
}

// Macro that creates a named local and returns a reference to it.
// Proves __cccc_ast_local_var works (not just the unique variant).
[[cccc::comptime(inline)]]
$node_t *make_local_forty_two(void) {
    $vm_t *vm = __cccc_get_vm();
    $type_t *ty_int = __cccc_ast_get_type(vm, "int");
    $node_t *var  = __cccc_ast_local_var(vm, "named_tmp", ty_int);
    $node_t *val  = __cccc_ast_int_literal(vm, 42);
    return __cccc_ast_assign(vm, var, val);
}

// Ticket #305: $local_var_unique inside $with_fn must land in the inner
// function's locals, not the outer macro's locals (vm->compiler.locals fix).
[[cccc::comptime]]
$node_t *gen_add_fn(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("local_var_add", int_ty);
    $with_fn(fn) {
        $node_t *tmp = $local_var_unique(int_ty);
        $node_t *asgn = $assign(tmp, $int_literal(3));
        $node_t *read = $local_var_unique(int_ty);
        $node_t *asgn2 = $assign(read, $int_literal(4));
        $node_t *sum = $binary(nk_add, asgn, asgn2);
        $function_set_body(fn, $return(sum));
    }
    return $int_literal(0);
}
gen_add_fn();

int main(void) {
    // doubled: unique temp should not capture any existing local
    int result = doubled(7);    // tmp = 7*2 = 14 (assign expr = 14)
    if (result != 14) return 1;

    int result2 = doubled(10);  // tmp = 10*2 = 20
    if (result2 != 20) return 2;

    // named local
    int v = make_local_forty_two();  // named_tmp = 42
    if (v != 42) return 3;

    // $local_var_unique inside $with_fn (ticket #305)
    if (local_var_add() != 7) return 4;

    return 42;
}
