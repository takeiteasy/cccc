// An attribute handler reads the memory orders, weak flag and operands of
// atomic operations and fences.

[[cccc::comptime]] void expect(int line, int ok) {
    if (!ok)
        MacroErrorAt(0, "expectation failed at test line %d", line);
}

[[cccc::comptime]] void collect(Node *n, Node **seen, int *nseen) {
    if (!n || *nseen >= 256)
        return;
    seen[(*nseen)++] = n;
    if (GetNodeKind(n) == NK_BLOCK || GetNodeKind(n) == NK_STMT_EXPR)
        for (int i = 0; i < BlockCount(n); i++)
            collect(BlockAt(n, i), seen, nseen);
    collect(NodeLhs(n), seen, nseen);
    collect(NodeRhs(n), seen, nseen);
    collect(NodeThen(n), seen, nseen);
    collect(NodeElse(n), seen, nseen);
}

[[cccc::comptime]] Node *nth(Node **seen, int nseen, int kind, int want) {
    for (int i = 0; i < nseen; i++)
        if (GetNodeKind(seen[i]) == kind && want-- == 0)
            return seen[i];
    return 0;
}

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *seen[256];
    int   nseen = 0;
    collect(FnBody(AttrTargetObj(target)), seen, &nseen);

    // __atomic_load_n(p, ACQUIRE), then __builtin_atomic_load(p): no order.
    expect(__LINE__, AtomicOrder(nth(seen, nseen, NK_ALOAD, 0)) == 2);
    expect(__LINE__, AtomicOrder(nth(seen, nseen, NK_ALOAD, 1)) == 5);
    expect(__LINE__, nth(seen, nseen, NK_ALOAD, 2) == 0);

    // __atomic_store_n(p, v, RELEASE)
    expect(__LINE__, AtomicOrder(nth(seen, nseen, NK_ASTORE, 0)) == 3);

    // __atomic_exchange_n(p, 3, ACQ_REL)
    Node *x = nth(seen, nseen, NK_EXCH, 0);
    expect(__LINE__, x && AtomicOrder(x) == 4);
    expect(__LINE__, AtomicFailOrder(x) == -1 && !AtomicIsWeak(x));

    // weak (ACQ_REL, ACQUIRE), then strong (SEQ_CST, RELAXED)
    Node *c0 = nth(seen, nseen, NK_CAS, 0), *c1 = nth(seen, nseen, NK_CAS, 1);
    expect(__LINE__, c0 && c1 && nth(seen, nseen, NK_CAS, 2) == 0);
    expect(__LINE__, AtomicIsWeak(c0) && !AtomicIsWeak(c1));
    expect(__LINE__, AtomicOrder(c0) == 4 && AtomicFailOrder(c0) == 2);
    expect(__LINE__, AtomicOrder(c1) == 5 && AtomicFailOrder(c1) == 0);
    expect(__LINE__, GetNodeKind(StripImplicit(CasAddr(c0))) == NK_VAR);
    expect(__LINE__, GetNodeKind(StripImplicit(CasExpected(c0))) == NK_VAR);
    expect(__LINE__, GetNodeKind(StripImplicit(CasDesired(c0))) == NK_VAR);
    expect(__LINE__, CasAddr(c0) != CasExpected(c0));

    // Thread fence, then signal fence.
    Node *f0 = nth(seen, nseen, NK_FENCE, 0),
         *f1 = nth(seen, nseen, NK_FENCE, 1);
    expect(__LINE__, f0 && f1 && nth(seen, nseen, NK_FENCE, 2) == 0);
    expect(__LINE__, !FenceIsSignal(f0) && FenceIsSignal(f1));

    // Other kinds and NULL: -1 for orders, since 0 is relaxed.
    Node *other = seen[0];
    expect(__LINE__, AtomicOrder(0) == -1 && AtomicOrder(other) == -1);
    expect(__LINE__, AtomicFailOrder(0) == -1 && AtomicFailOrder(other) == -1);
    expect(__LINE__, !AtomicIsWeak(0) && !AtomicIsWeak(other));
    expect(__LINE__, CasAddr(other) == 0 && CasExpected(0) == 0);
    expect(__LINE__, CasDesired(nth(seen, nseen, NK_ALOAD, 0)) == 0);
    expect(__LINE__, !FenceIsSignal(0) && !FenceIsSignal(other));

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int atoms(int *p, int *expected, int desired) {
    int v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
    int old = __atomic_exchange_n(p, 3, __ATOMIC_ACQ_REL);
    int ok  = __atomic_compare_exchange_n(p, expected, desired, 1,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    int ok2 = __atomic_compare_exchange_n(p, expected, desired, 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
    v       = __builtin_atomic_load(p);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    return v + old + ok + ok2;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
