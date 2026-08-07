// CCCC_FLAGS: --checked-pointers
// Checked-pointer bounds on a prototype-only declaration (#770/#483): a
// bound referencing a later parameter must compile clean even though the
// declaration has no body, so function() never opens a scope to resolve it.
// The token span is left permanently unresolved, which is correct, not an
// error -- caller-side checking of the bound is future work (#488).

void f(int * [[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    return 42;
}
