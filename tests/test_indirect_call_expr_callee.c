// Ticket V010 (#885): an indirect call whose callee EXPRESSION itself
// contains a function call -- e.g. `((int(*)(int,int))dlsym(h,"f"))(a,b)` --
// crashed/mis-executed. gen_expr's ND_FUNCALL case staged arguments into
// REG_A0-A7 *before* evaluating the callee expression; if that expression
// contained its own call, the nested call's own argument setup clobbered
// the already-staged outer args. Confirmed to be a general codegen bug, not
// FFI- or comptime-specific -- the plain-guest shape below reproduced it
// with no dlopen/dlsym involved (wrong result, not even a crash: the outer
// arg 0 got silently replaced by the nested call's return value).
//
// Expected return: 42

int add(int a, int b) {
    return a + b;
}

int (*get_add(void))(int, int) {
    return add;
}

// Primary repro: callee expression `get_add()` is itself a call.
int test_inline_callee_call(void) {
    return get_add()(19, 23);
}

// Args-side companion: staging order is being touched by the fix, so make
// sure `f(g(), h())`-shaped argument lists (calls as arguments, not as the
// callee) still work.
int sum3(int a, int b, int c) {
    return a + b + c;
}
int one(void) {
    return 1;
}
int two(void) {
    return 2;
}

int test_call_args_containing_calls(void) {
    return sum3(one(), two(), 39);
}

// Overflow-arg variant: 9+ arguments exercises the stack-pushed overflow-arg
// path alongside the callee hoist/spill.
int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return a + b + c + d + e + f + g + h + i;
}
int (*get_sum9(void))(int, int, int, int, int, int, int, int, int) {
    return sum9;
}

int test_inline_callee_call_overflow_args(void) {
    return get_sum9()(1, 2, 3, 4, 5, 6, 7, 8, 9);
}

int main(void) {
    if (test_inline_callee_call() != 42)
        return 1;
    if (test_call_args_containing_calls() != 42)
        return 2;
    if (test_inline_callee_call_overflow_args() != 45)
        return 3;
    return 42;
}
