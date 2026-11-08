// A comptime call in expression position finds bodies of functions defined
// before and after it, and those bodies are typed.

[[cccc::comptime]] Node *shape(Node *which) {
    Obj *fn = FindGlobal(NodeIntValue(which) ? "later" : "earlier");
    if (!fn || !ObjIsDefinition(fn))
        return MakeIntLiteral(0);
    Node *body = FnBody(fn);
    if (!body || GetNodeKind(body) != NK_BLOCK)
        return MakeIntLiteral(0);
    Node *ret = BlockAt(body, 0);
    if (GetNodeKind(ret) != NK_RETURN)
        return MakeIntLiteral(0);
    // The returned expression is typed: its type is int.
    Node *value = StripImplicit(NodeLhs(ret));
    if (GetNodeKind(value) != NK_NUM ||
        GetTypeKind(GetNodeType(value)) != TK_INT)
        return MakeIntLiteral(0);
    return MakeIntLiteral((int)NodeIntValue(value));
}

int earlier(void) {
    return 20;
}

int main(void) {
    return shape(0) + shape(1);
}

int later(void) {
    return 22;
}
