// An attribute handler reads the operation and width of the bit-manipulation
// builtins and the operation and result pointer of checked arithmetic.

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

[[cccc::comptime]] void check_bitop(int line, Node *n, int op, int width) {
    if (!n || BitopOp(n) != op || BitopWidth(n) != width)
        MacroErrorAt(0, "bitop wrong at test line %d", line);
}

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *seen[256];
    int   nseen = 0;
    collect(FnBody(AttrTargetObj(target)), seen, &nseen);

    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 0), AST_BITOP_CLZ, 32);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 1), AST_BITOP_CLZ, 64);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 2), AST_BITOP_CTZ, 32);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 3), AST_BITOP_CTZ, 64);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 4), AST_BITOP_POPCOUNT, 0);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 5), AST_BITOP_POPCOUNT, 0);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 6), AST_BITOP_PARITY, 0);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 7), AST_BITOP_FFS, 32);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 8), AST_BITOP_FFS, 64);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 9), AST_BITOP_BSWAP, 16);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 10), AST_BITOP_BSWAP, 32);
    check_bitop(__LINE__, nth(seen, nseen, NK_BITOP, 11), AST_BITOP_BSWAP, 64);
    expect(__LINE__, nth(seen, nseen, NK_BITOP, 12) == 0);

    // The operand is the argument.
    expect(__LINE__, GetNodeKind(StripImplicit(
                         NodeLhs(nth(seen, nseen, NK_BITOP, 0)))) == NK_VAR);

    Node *o0 = nth(seen, nseen, NK_OVERFLOW_ARITH, 0),
         *o1 = nth(seen, nseen, NK_OVERFLOW_ARITH, 1);
    Node *o2 = nth(seen, nseen, NK_OVERFLOW_ARITH, 2);
    expect(__LINE__,
           o0 && o1 && o2 && nth(seen, nseen, NK_OVERFLOW_ARITH, 3) == 0);
    expect(__LINE__, OverflowOp(o0) == AST_OVERFLOW_ADD);
    expect(__LINE__, OverflowOp(o1) == AST_OVERFLOW_SUB);
    expect(__LINE__, OverflowOp(o2) == AST_OVERFLOW_MUL);
    expect(__LINE__,
           GetNodeKind(StripImplicit(OverflowResultPtr(o0))) == NK_ADDR);
    expect(__LINE__, NodeIntValue(StripImplicit(NodeRhs(o2))) == 2);

    // Other kinds and NULL.
    Node *other = seen[0];
    expect(__LINE__, BitopOp(0) == 0 && BitopOp(other) == 0);
    expect(__LINE__, BitopWidth(0) == 0 && BitopWidth(other) == 0);
    expect(__LINE__, OverflowOp(0) == 0 && OverflowOp(other) == 0);
    expect(__LINE__,
           OverflowResultPtr(0) == 0 && OverflowResultPtr(other) == 0);
    expect(__LINE__, OverflowOp(nth(seen, nseen, NK_BITOP, 0)) == 0);
    expect(__LINE__, BitopOp(o0) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int bits(unsigned a, unsigned long long b) {
    int x  = 0;
    x     += __builtin_clz(a) + __builtin_clzll(b);
    x     += __builtin_ctz(a) + __builtin_ctzll(b);
    x     += __builtin_popcount(a) + __builtin_popcountll(b);
    x     += __builtin_parity(a);
    x     += __builtin_ffs(a) + __builtin_ffsll(b);
    x     += __builtin_bswap16(a) + __builtin_bswap32(a) + __builtin_bswap64(b);
    x     += __builtin_add_overflow(a, 1, &x);
    x     += __builtin_sub_overflow(a, 1, &x);
    x     += __builtin_mul_overflow(a, 2, &x);
    return x;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
