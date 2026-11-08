// StripImplicit peels compiler-inserted conversions but keeps a cast written
// in the source.

[[cccc::comptime]] void expect(int line, int ok) {
    if (!ok)
        MacroErrorAt(0, "expectation failed at test line %d", line);
}

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *body   = FnBody(AttrTargetObj(target));
    Node *first  = 0;
    Node *second = 0;
    for (int i = 0; i < BlockCount(body); i++) {
        Node *s = BlockAt(body, i);
        if (GetNodeKind(s) != NK_EXPR_STMT ||
            GetNodeKind(NodeLhs(s)) != NK_ASSIGN)
            continue;
        if (!first)
            first = NodeLhs(s);
        else
            second = NodeLhs(s);
    }
    expect(__LINE__, first && second);

    // r = (long)a;  keeps the explicit cast
    Node *cast = StripImplicit(NodeRhs(first));
    expect(__LINE__, GetNodeKind(cast) == NK_CAST);
    expect(__LINE__, GetNodeKind(StripImplicit(NodeLhs(cast))) == NK_VAR);

    // r = a + 1;  the promotion casts around the sum and its operands go
    Node *sum = StripImplicit(NodeRhs(second));
    expect(__LINE__, GetNodeKind(sum) == NK_ADD);
    expect(__LINE__, GetNodeKind(StripImplicit(NodeLhs(sum))) == NK_VAR);
    expect(__LINE__, GetNodeKind(StripImplicit(NodeRhs(sum))) == NK_NUM);

    // Nodes that are not casts are returned as they are; NULL is tolerated.
    expect(__LINE__, StripImplicit(sum) == sum);
    expect(__LINE__, StripImplicit(0) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int casts(int a) {
    long r;
    r = (long)a;
    r = a + 1;
    return (int)r;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
