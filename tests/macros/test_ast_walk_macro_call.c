// An attribute handler runs before comptime calls in the body are expanded,
// so the walker sees them as NK_MACRO_CALL nodes with their name and arguments.

[[cccc::comptime]] void expect(int line, int ok) {
    if (!ok)
        MacroErrorAt(0, "expectation failed at test line %d", line);
}

[[cccc::comptime]] Node *answer(int a) {
    return MakeIntLiteral(a);
}

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *body  = FnBody(AttrTargetObj(target));
    Node *found = 0;
    for (int i = 0; i < BlockCount(body); i++) {
        Node *s = BlockAt(body, i);
        if (GetNodeKind(s) == NK_RETURN)
            found = StripImplicit(NodeLhs(s));
    }
    expect(__LINE__, found != 0);
    expect(__LINE__, GetNodeKind(found) == NK_MACRO_CALL);
    expect(__LINE__, MacroCallName(found)[0] == 'a');
    expect(__LINE__, MacroCallArgCount(found) == 1);
    expect(__LINE__, MacroCallArgAt(found, 1) == 0);
    expect(__LINE__, NodeIntValue(MacroCallArgAt(found, 0)) == 42);

    // Wrong-kind inputs.
    expect(__LINE__, MacroCallName(body) == 0);
    expect(__LINE__, MacroCallArgCount(body) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int uses_macro(void) {
    return answer(42);
}

int walk_result(void);

int main(void) {
    return walk_result();
}
