// Test ticket #1: __builtin_quote / __builtin_quote_n quasi-quoting

// ---- Expression splice: positional $N ------------------------------------
// Produces: a + b * a  (reuses $1)
[[cccc::comptime(inline)]]
Node *add_mul(Node *a, Node *b) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "$1 + $2 * $1", a, b);
}

// ---- Expression splice: reorder ($2 * $1 + $2) ---------------------------
// Produces: y*x + y
[[cccc::comptime(inline)]]
Node *quad(Node *x, Node *y) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "$2 * $1 + $2", x, y);
}

// ---- Statement splice: return $1; ----------------------------------------
// Must be called in statement position (not as an expression).
[[cccc::comptime(inline)]]
Node *ret_val(Node *v) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "return $1;", v);
}

int get_99(void) {
    ret_val(99);
}

// ---- $$ incremental sugar ------------------------------------------------
// $$ maps to $1, $2, ... sequentially (left-to-right).
// Produces: a + b
[[cccc::comptime(inline)]]
Node *sum_incr(Node *a, Node *b) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "$$ + $$", a, b);
}

// ---- __builtin_quote_n: array form ---------------------------------------------
// Produces: a + b + c
[[cccc::comptime(inline)]]
Node *add3(Node *a, Node *b, Node *c) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *args[3] = { a, b, c };
    return __builtin_quote_n(vm, "$1 + $2 + $3", args, 3);
}

// ---- No splice points (plain expression) ---------------------------------
[[cccc::comptime(inline)]]
Node *const_expr(void) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "6 * 7");
}

// ---- If-statement template -----------------------------------------------
// Returns one of two values based on sign; invoked as statement in function body.
[[cccc::comptime(inline)]]
Node *clamp_zero(Node *val) {
    VirtualMachine *vm = __builtin_get_vm();
    return __builtin_quote(vm, "if ($1 < 0) return 0; else return $1;", val);
}

int clamp(int x) {
    clamp_zero(x);
}

int main(void) {
    // add_mul: 3 + 5 * 3 = 3 + 15 = 18
    int r1 = add_mul(3, 5);
    if (r1 != 18) return 1;

    // quad: 5*3 + 5 = 15 + 5 = 20
    int r2 = quad(3, 5);
    if (r2 != 20) return 2;

    // ret_val: function wrapping a statement macro
    if (get_99() != 99) return 3;

    // sum_incr ($$): 10 + 20 = 30
    int r4 = sum_incr(10, 20);
    if (r4 != 30) return 4;

    // add3: 1 + 2 + 3 = 6
    int r5 = add3(1, 2, 3);
    if (r5 != 6) return 5;

    // const_expr: 6 * 7 = 42
    int r6 = const_expr();
    if (r6 != 42) return 6;

    // clamp: negative -> 0, positive -> unchanged
    if (clamp(-5) != 0) return 7;
    if (clamp(7)  != 7) return 8;

    return 42;
}
