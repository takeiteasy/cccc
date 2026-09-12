// CCCC_FLAGS: --checked-pointers
// Checked-pointer bounds on a prototype-only declaration (#770/#483): a
// bound referencing a later parameter must compile clean even though the
// declaration has no body, so function() never opens a scope to resolve it.
// The token span itself is left permanently unresolved (Obj.checked_
// bounds_lo/hi have no Obj to resolve into, since a prototype has no
// scope) -- but is no longer dead: #488's resolve_param_checked_bounds()
// (src/parse_core.c) resolves the SAME token span, separately and lazily,
// into a caller-side TEMPLATE the first time any call site is compiled,
// consuming exactly what this comment used to describe as future work.
// See tests/test_checked_call_arg_count_trap.c for the caller-side check
// itself; this file stays focused on the narrower "a prototype with no
// body compiles clean" claim its name describes.

void f(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    return 42;
}
