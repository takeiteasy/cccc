// CCCC_FLAGS: -O1
// Test dead-call elimination for CALLN (indirect calls).
// When an indirect call's result is unused and the function pointer type
// carries a pure/const annotation, the call dispatch is skipped at -O1+.
// Argument and function-pointer evaluation side effects must still execute.

// [[gnu::const]] annotated function
[[gnu::const]] static int triple(int x) { return x * 3; }
// [[gnu::pure]] annotated function
[[gnu::pure]]  static int double_it(int x) { return x * 2; }

// Declare function pointer types that carry the annotations so CALLN
// dead-call elimination can see them via node->func_ty.
typedef int (__attribute__((const))  *const_fn_t)(int);
typedef int (__attribute__((pure))   *pure_fn_t)(int);

int main(void) {
    int n = 3;

    // CALLN via a const-annotated function pointer type: result unused.
    // Dead-call elimination should skip the call dispatch while ++n runs.
    const_fn_t fp_c = triple;
    fp_c(++n);                  // n becomes 4; call result unused
    if (n != 4) return 1;

    // CALLN via a pure-annotated function pointer type: result unused.
    pure_fn_t fp_p = double_it;
    fp_p(++n);                  // n becomes 5; call result unused
    if (n != 5) return 2;

    // Result used: call must still execute correctly.
    if (triple(3) != 9) return 3;
    if (double_it(4) != 8) return 4;

    // Multiple unused indirect calls: all side effects must run.
    int k = 0;
    fp_c(++k);
    fp_c(++k);
    if (k != 2) return 5;

    return 42;
}
