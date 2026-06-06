// Test ticket #194: $@N / $@ unquote-splicing in call-argument-list position.
// main() returns 42 on success.

#include "stdarg.h"

// ---- Helper variadic functions -----------------------------------------------

// Sum count integers passed as variadic args.
int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += va_arg(args, int);
    va_end(args);
    return sum;
}

// Fixed prefix (base) + variadic count + ints.
int base_plus_sum(int base, int count, ...) {
    va_list args;
    va_start(args, count);
    int total = base;
    for (int i = 0; i < count; i++)
        total += va_arg(args, int);
    va_end(args);
    return total;
}

// ---- Test 1: splice full arg list into a variadic callee ---------------------
// call_sum3 builds a three-element node chain and splices it as the variadic
// portion of sum_ints(3, ...).
[[jcc::macro(inline)]]
$node_t *call_sum3($node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = __jcc_node_list(vm, ($node_t*[]){ a, b, c }, 3);
    return __jcc_quote(vm, "sum_ints(3, $@1)", chain);
}

int test_full_variadic_splice(void) {
    return call_sum3(10, 20, 12); // sum_ints(3, 10, 20, 12) == 42
}

// ---- Test 2: splice into variadic tail after a fixed prefix arg --------------
// call_bps passes a fixed base, then splices two more ints into the variadic.
[[jcc::macro(inline)]]
$node_t *call_bps($node_t *base, $node_t *x, $node_t *y) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = __jcc_node_list(vm, ($node_t*[]){ x, y }, 2);
    return __jcc_quote(vm, "base_plus_sum($1, 2, $@2)", base, chain);
}

int test_prefix_then_splice(void) {
    return call_bps(10, 16, 16); // base_plus_sum(10, 2, 16, 16) == 42
}

// ---- Test 3: empty splice (zero variadic args inserted) ----------------------
// When __jcc_node_list returns NULL the $@k placeholder expands to nothing,
// leaving sum_ints with only the fixed count argument.
[[jcc::macro(inline)]]
$node_t *call_sum_empty($node_t *fixed) {
    $vm_t *vm = __jcc_get_vm();
    // count == 0 → __jcc_node_list returns NULL → empty splice
    $node_t *empty = __jcc_node_list(vm, ($node_t*[]){}, 0);
    return __jcc_quote(vm, "sum_ints($1, $@2)", fixed, empty);
}

int test_empty_splice(void) {
    return call_sum_empty(0); // sum_ints(0) == 0
}

// ---- Test 4: positional $@2 with a scalar $1 --------------------------------
// Mixed scalar + splice in the same template.
[[jcc::macro(inline)]]
$node_t *call_mixed($node_t *base, $node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = __jcc_node_list(vm, ($node_t*[]){ a, b, c }, 3);
    // $1 binds to base (scalar), $@2 splices the chain.
    return __jcc_quote(vm, "base_plus_sum($1, 3, $@2)", base, chain);
}

int test_mixed_scalar_splice(void) {
    return call_mixed(12, 10, 10, 10); // base_plus_sum(12, 3, 10, 10, 10) == 42
}

// ---- main -------------------------------------------------------------------

int main(void) {
    if (test_full_variadic_splice() != 42) return 1;
    if (test_prefix_then_splice()   != 42) return 2;
    if (test_empty_splice()         !=  0) return 3;
    if (test_mixed_scalar_splice()  != 42) return 4;

    return 42;
}
