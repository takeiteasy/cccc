// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: floating-point comparison is an error: use a tolerance
// The lint from docs/macros-ast-walk.md rejects a floating-point `==`.

[[cccc::comptime]] void check(Node *n);
[[cccc::comptime]] void check(Node *n) {
    if (!n)
        return;
    NodeKind k = GetNodeKind(n);
    if (k == NK_BLOCK || k == NK_STMT_EXPR) {
        for (int i = 0; i < BlockCount(n); i++)
            check(BlockAt(n, i));
        return;
    }
    if (k == NK_EQ || k == NK_NE) {
        Type *t = GetNodeType(StripImplicit(NodeLhs(n)));
        if (t && (GetTypeKind(t) == TK_DOUBLE || GetTypeKind(t) == TK_FLOAT))
            MacroErrorAt(
                0, "floating-point comparison is an error: use a tolerance");
    }
    check(NodeCond(n));
    check(NodeThen(n));
    check(NodeElse(n));
    check(NodeLhs(n));
    check(NodeRhs(n));
}

@comptime(attribute("no_float_eq")) void no_float_eq(AttrTarget *target) {
    check(FnBody(AttrTargetObj(target)));
}

@no_float_eq int same(double a, double b) {
    if (a == b)
        return 1;
    return 0;
}

int main(void) {
    return same(1.0, 1.0);
}
