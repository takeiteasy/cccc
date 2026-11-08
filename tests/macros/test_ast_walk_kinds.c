// A custom attribute handler walks a defined function: parameters, locals and
// the typed statement tree.

[[cccc::comptime]] void expect(int line, int ok) {
    if (!ok)
        MacroErrorAt(0, "expectation failed at test line %d", line);
}

struct Pair {
    int lo;
    int hi;
};

int helper(int a, int b);

@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Obj *fn = AttrTargetObj(target);
    expect(__LINE__, ObjIsFunction(fn));

    expect(__LINE__, FnParamCount(fn) == 2);
    expect(__LINE__, ObjName(FnParamAt(fn, 0))[0] == 'p');
    expect(__LINE__, ObjName(FnParamAt(fn, 1))[0] == 'n');
    expect(__LINE__, FnParamAt(fn, 2) == 0);
    expect(__LINE__, FnLocalCount(fn) == 3);
    expect(__LINE__, ObjName(FnLocalAt(fn, 0))[0] == 'x');
    expect(__LINE__, ObjName(FnLocalAt(fn, 1))[0] == 'r');
    expect(__LINE__, ObjName(FnLocalAt(fn, 2))[0] == 'i');
    expect(__LINE__, FnLocalAt(fn, 3) == 0);

    Node *body = FnBody(fn);
    expect(__LINE__, GetNodeKind(body) == NK_BLOCK);

    // Locals are listed in declaration order; find statements by kind.
    Node *if_node = 0, *for_node = 0, *call_stmt = 0, *ret = 0, *goto_node = 0;
    for (int i = 0; i < BlockCount(body); i++) {
        Node *s = BlockAt(body, i);
        if (GetNodeKind(s) == NK_IF)
            if_node = s;
        else if (GetNodeKind(s) == NK_FOR)
            for_node = s;
        else if (GetNodeKind(s) == NK_RETURN)
            ret = s;
        else if (GetNodeKind(s) == NK_GOTO)
            goto_node = s;
        else if (GetNodeKind(s) == NK_EXPR_STMT &&
                 GetNodeKind(NodeLhs(s)) == NK_FUNCALL)
            call_stmt = s;
    }
    expect(__LINE__, if_node && for_node && call_stmt && ret && goto_node);

    // if (x > 3) ...
    Node *cond = StripImplicit(NodeCond(if_node));
    expect(__LINE__, GetNodeKind(cond) == NK_LT);
    expect(__LINE__, NodeIntValue(StripImplicit(NodeLhs(cond))) == 3);
    expect(__LINE__, GetNodeKind(StripImplicit(NodeRhs(cond))) == NK_VAR);
    expect(__LINE__,
           ObjName(NodeVarObj(StripImplicit(NodeRhs(cond))))[0] == 'x');
    expect(__LINE__, GetNodeKind(NodeThen(if_node)) == NK_RETURN);
    expect(__LINE__, NodeElse(if_node) == 0);

    // for (i = 0; i < n; i++) ...
    expect(__LINE__, NodeInit(for_node) != 0);
    expect(__LINE__, NodeCond(for_node) != 0);
    expect(__LINE__, NodeInc(for_node) != 0);
    expect(__LINE__, GetNodeKind(NodeThen(for_node)) != NK_NULL_EXPR);

    // helper(x, 7)
    Node *call = NodeLhs(call_stmt);
    expect(__LINE__, ObjName(NodeVarObj(CallCallee(call)))[0] == 'h');
    expect(__LINE__, CallArgCount(call) == 2);
    expect(__LINE__, CallArgAt(call, 2) == 0);
    Node *seven = StripImplicit(CallArgAt(call, 1));
    expect(__LINE__, GetNodeKind(seven) == NK_NUM);
    expect(__LINE__, NodeIntValue(seven) == 7);
    expect(__LINE__, NodeFloatValue(seven) == 0.0);
    expect(__LINE__, GetTypeKind(GetNodeType(seven)) == TK_INT);

    // return p->hi;
    Node *member = StripImplicit(NodeLhs(ret));
    while (GetNodeKind(member) != NK_MEMBER && NodeLhs(member))
        member = StripImplicit(NodeLhs(member));
    expect(__LINE__, GetNodeKind(member) == NK_MEMBER);
    expect(__LINE__, NodeMember(member) != 0);

    // A literal that is not an integer reads back as 0 and vice versa.
    expect(__LINE__, NodeIntValue(member) == 0);
    expect(__LINE__, CallCallee(member) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int sample(struct Pair *p, long n) {
    int x = 1;
    int r;
    if (3 < x)
        return 0;
    for (int i = 0; i < n; i++)
        x = x * 2;
    helper(x, 7);
    goto done;
done:
    r = p->hi;
    return p->hi + r;
}

int helper(int a, int b) {
    return a + b;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
