// Test ticket #172: $@N / $@ unquote-splicing in statement-list position.
// main() returns 42 on success.

// ---- Positional list splice: $@1 ------------------------------------------
// double_inc builds a two-statement chain (x+=1; x+=1;) and splices it
// into a block using the positional $@1 splice.
[[cccc::comptime(inline)]]
Node *double_inc(Node *x) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *s1    = __builtin_quote(vm, "$1 += 1;", x);
    Node *s2    = __builtin_quote(vm, "$1 += 1;", x);
    Node *chain = __builtin_node_list(vm, (Node*[]){ s1, s2 }, 2);
    return __builtin_quote(vm, "{ $@1; }", chain);
}

int get_plus_two(int v) {
    double_inc(v);
    return v;
}

// ---- __builtin_node_list + $@1 (three-element chain) ---------------------------
// accumulate3 takes an accumulator variable and three addends; builds a
// three-statement chain and splices it.  All inner templates reference only
// macro arguments (which are call-site nodes, in scope at macro execution).
[[cccc::comptime(inline)]]
Node *accumulate3(Node *acc, Node *a, Node *b, Node *c) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *s1    = __builtin_quote(vm, "$1 += $2;", acc, a);
    Node *s2    = __builtin_quote(vm, "$1 += $2;", acc, b);
    Node *s3    = __builtin_quote(vm, "$1 += $2;", acc, c);
    Node *chain = __builtin_node_list(vm, (Node*[]){ s1, s2, s3 }, 3);
    return __builtin_quote(vm, "{ $@1; }", chain);
}

int test_accumulate(void) {
    int acc = 0;
    accumulate3(acc, 1, 2, 3);
    return acc;
}

// ---- Incremental splice $@ (two statements) --------------------------------
// two_increments builds two stmts and splices them via incremental $@.
[[cccc::comptime(inline)]]
Node *two_increments(Node *a, Node *b) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *s1 = __builtin_quote(vm, "$1 += 10;", a);
    Node *s2 = __builtin_quote(vm, "$1 += 20;", b);
    return __builtin_quote(vm, "{ $@; $@; }", s1, s2);
}

int test_incr(void) {
    int x = 0, y = 0;
    two_increments(x, y);
    return x + y;
}

// ---- Mixed scalar $1 and list splice $@2 -----------------------------------
// if_then uses scalar $1 for the condition and list splice $@2 for the body.
[[cccc::comptime(inline)]]
Node *if_then(Node *cond, Node *body_expr) {
    VirtualMachine *vm = __builtin_get_vm();
    // Wrap the expression argument in a statement so it can be list-spliced.
    Node *stmt = __builtin_quote(vm, "$1;", body_expr);
    return __builtin_quote(vm, "{ if ($1) { $@2; } }", cond, stmt);
}

int test_if_then(void) {
    int x = 0;
    if_then(1, x += 7);
    return x;
}

// ---- Locals stress test: native compound-assign + splice in same fn --------
// This function has its OWN compound assignments (which create pointer-temp
// lvars at parse time) AND calls a splice macro that injects more compound
// assignments (creating additional pointer-temp lvars at expand time).  If
// the locals-flush fix is correct, all lvars get distinct stack slots and
// neither the native nor the injected increments clobber each other.
int test_locals_mix(int base) {
    int native = base;
    native += 100;          // native compound assign — lvar at parse time
    double_inc(native);     // splice macro injects two more compound assigns
    native += 5;            // another native compound assign
    return native;          // base + 100 + 2 + 5
}

int main(void) {
    // double_inc: v = 5, two increments -> 7
    if (get_plus_two(5) != 7) return 1;

    // accumulate3: acc = 0 + 1 + 2 + 3 = 6
    if (test_accumulate() != 6) return 2;

    // two_increments: x += 10, y += 20, sum = 30
    if (test_incr() != 30) return 3;

    // if_then: cond=1, body x += 7 -> x = 7
    if (test_if_then() != 7) return 4;

    // locals_mix: base=0 -> 0 + 100 + 2 + 5 = 107
    if (test_locals_mix(0) != 107) return 5;

    return 42;
}
