// Test ticket #77: __builtin_ast_local_var / __builtin_ast_local_var_unique

// Macro that creates a hygienic temporary, assigns arg*2 to it, and returns it.
// Demonstrates that the gensym'd name doesn't collide with user locals.
[[cccc::comptime]]
Node *doubled(Node *arg) {
    VirtualMachine *vm = __builtin_get_vm();

    // Inject a unique (gensym'd) local of type int
    Type *ty_int = __builtin_ast_get_type(vm, "int");
    Node *tmp = __builtin_ast_local_var_unique(vm, ty_int);

    // Build: tmp = arg * 2
    Node *two   = __builtin_ast_int_literal(vm, 2);
    Node *mul   = __builtin_ast_binary(vm, NK_MUL, arg, two);
    Node *asgn  = __builtin_ast_assign(vm, tmp, mul);

    // Return tmp (the assignment expression already set tmp, but return tmp ref
    // as an ND_ASSIGN evaluates to the assigned value — return the assign expr)
    return asgn;
}

// Macro that creates a named local and returns a reference to it.
// Proves __builtin_ast_local_var works (not just the unique variant).
[[cccc::comptime]]
Node *make_local_forty_two(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Type *ty_int = __builtin_ast_get_type(vm, "int");
    Node *var  = __builtin_ast_local_var(vm, "named_tmp", ty_int);
    Node *val  = __builtin_ast_int_literal(vm, 42);
    return __builtin_ast_assign(vm, var, val);
}

// Ticket #305: MakeLocalVarUnique inside WithFn must land in the inner
// function's locals, not the outer macro's locals (vm->compiler.locals fix).
[[cccc::comptime]]
Node *gen_add_fn(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("local_var_add", int_ty);
    WithFn(fn) {
        Node *tmp = MakeLocalVarUnique(int_ty);
        Node *asgn = MakeAssign(tmp, MakeIntLiteral(3));
        Node *read = MakeLocalVarUnique(int_ty);
        Node *asgn2 = MakeAssign(read, MakeIntLiteral(4));
        Node *sum = MakeBinary(NK_ADD, asgn, asgn2);
        FunctionSetBody(fn, MakeReturn(sum));
    }
    return MakeIntLiteral(0);
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

    // MakeLocalVarUnique inside WithFn (ticket #305)
    if (local_var_add() != 7) return 4;

    return 42;
}
