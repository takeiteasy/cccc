// Switch cases are returned in source order, ranges keep both ends, and the
// default label is separate.

[[cccc::comptime]] void expect(int line, int ok) {
    if (!ok)
        MacroErrorAt(0, "expectation failed at test line %d", line);
}

[[cccc::comptime]] Node *find_switch(Node *n) {
    if (!n)
        return 0;
    if (GetNodeKind(n) == NK_SWITCH)
        return n;
    if (GetNodeKind(n) == NK_BLOCK) {
        for (int i = 0; i < BlockCount(n); i++) {
            Node *r = find_switch(BlockAt(n, i));
            if (r)
                return r;
        }
    }
    return 0;
}

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *sw = find_switch(FnBody(AttrTargetObj(target)));
    expect(__LINE__, sw);
    expect(__LINE__, SwitchCaseCount(sw) == 3);
    expect(__LINE__, CaseBegin(SwitchCaseAt(sw, 0)) == 1);
    expect(__LINE__, CaseBegin(SwitchCaseAt(sw, 1)) == 5);
    expect(__LINE__, CaseEnd(SwitchCaseAt(sw, 1)) == 9);
    expect(__LINE__, CaseBegin(SwitchCaseAt(sw, 2)) == 20);
    expect(__LINE__, SwitchCaseAt(sw, 3) == 0);
    expect(__LINE__, SwitchCaseAt(sw, -1) == 0);
    expect(__LINE__, GetNodeKind(SwitchCaseAt(sw, 0)) == NK_CASE);
    expect(__LINE__, CaseBody(SwitchCaseAt(sw, 0)) != 0);
    expect(__LINE__, GetNodeKind(SwitchDefault(sw)) == NK_CASE);
    expect(__LINE__, GetNodeKind(NodeCond(sw)) != NK_NULL_EXPR);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int classify(int v) {
    int out = 0;
    switch (v) {
        case 1:
            out = 10;
            break;
        case 5 ... 9:
            out = 20;
            break;
        case 20:
            out = 30;
            break;
        default:
            out = 40;
    }
    return out;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
