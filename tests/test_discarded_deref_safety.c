// CCCC_FLAGS: -3
// Ticket #916: a discarded-value dereference (`*p;`, `(void)*p`) evaluates
// its address with dest_reg == REG_ZERO. gen_expr's ND_DEREF/ND_MEMBER cases
// used to reuse dest_reg as scratch for that address, and REG_ZERO is
// hardwired to always read as 0 and discard writes -- so the address was
// silently address 0, not the real pointer. Integer loads looked harmless
// (op_LDR_*_fn skips the load entirely when rd == REG_ZERO) but float/double
// loads (FLDR/FLDR_F32) have no such guard and segfaulted (the original bug
// report). The same bogus-address bug also broke `-3`'s dangling-pointer
// safety check on a *valid* discarded deref: emit_load_safety_checks ran
// against address 0 and falsely reported "Attempted to dereference NULL
// pointer" even though `p` was a live, in-bounds pointer. This test is the
// regression check for that false positive -- every deref below is through
// a valid, live pointer and must run to completion under -3.
struct S {
    float f;
    int i;
};

int main(void) {
    int x = 5;
    int *pi = &x;
    *pi;          // bare discarded int deref
    (void)*pi;    // explicit (void)-discard

    float fx = 5.0f;
    float *pf = &fx;
    *pf;
    (void)*pf;

    double dx = 5.0;
    double *pd = &dx;
    *pd;

    struct S s = {1.0f, 2};
    struct S *ps = &s;
    ps->f;  // discarded member load through a pointer (float)
    ps->i;  // discarded member load through a pointer (int)
    s.f;    // discarded member load, no pointer indirection (float)

    float arr[3] = {1, 2, 3};
    int idx = 1;
    arr[idx]; // discarded indexed load (exercises emit_indexed_load_if_possible)

    int z = (*pi, 3); // discarded deref as a comma-operator LHS

    return 42;
}
