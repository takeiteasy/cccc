// An attribute handler reads the label name and jump kind of goto, break,
// continue, labels and labels-as-values.

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
    collect(NodeCond(n), seen, nseen);
    collect(NodeThen(n), seen, nseen);
    collect(NodeElse(n), seen, nseen);
    collect(NodeInit(n), seen, nseen);
    collect(NodeInc(n), seen, nseen);
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

    // goto first; break; continue; goto *t; in source order.
    Node *g0 = nth(seen, nseen, NK_GOTO, 0), *g1 = nth(seen, nseen, NK_GOTO, 1),
         *g2 = nth(seen, nseen, NK_GOTO, 2);
    expect(__LINE__, g0 && g1 && g2 && nth(seen, nseen, NK_GOTO, 3) == 0);
    int plain = 0, brk = 0, cont = 0;
    for (int i = 0; i < 3; i++) {
        Node *g = nth(seen, nseen, NK_GOTO, i);
        if (GotoJumpKind(g) == AST_JUMP_GOTO) {
            plain++;
            expect(__LINE__, LabelName(g)[0] == 'f');
        } else if (GotoJumpKind(g) == AST_JUMP_BREAK) {
            brk++;
            expect(__LINE__, LabelName(g) == 0);
        } else if (GotoJumpKind(g) == AST_JUMP_CONTINUE) {
            cont++;
            expect(__LINE__, LabelName(g) == 0);
        }
    }
    expect(__LINE__, plain == 1 && brk == 1 && cont == 1);

    // first: and second:
    Node *l0 = nth(seen, nseen, NK_LABEL, 0),
         *l1 = nth(seen, nseen, NK_LABEL, 1);
    expect(__LINE__, l0 && l1 && nth(seen, nseen, NK_LABEL, 2) == 0);
    expect(__LINE__, LabelName(l0)[0] == 'f' && LabelName(l1)[0] == 's');
    expect(__LINE__, GotoJumpKind(l0) == 0);

    // &&second, and goto *t has an expression target, not a name.
    Node *lv = nth(seen, nseen, NK_LABEL_VAL, 0);
    expect(__LINE__, lv && LabelName(lv)[0] == 's');
    Node *ge = nth(seen, nseen, NK_GOTO_EXPR, 0);
    expect(__LINE__, ge && NodeLhs(ge) != 0);
    expect(__LINE__, LabelName(ge) == 0 && GotoJumpKind(ge) == 0);

    // Other kinds and NULL.
    expect(__LINE__, LabelName(0) == 0 && GotoJumpKind(0) == 0);
    expect(__LINE__, LabelName(seen[0]) == 0 && GotoJumpKind(seen[0]) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect int jumps(int n) {
    int   i = 0;
    void *t = &&second;
    for (i = 0; i < n; i++) {
        if (i == 1)
            continue;
        if (i == 2)
            break;
    }
    goto first;
first:
    i = 1;
second:
    if (n < 0)
        goto *t;
    return i;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
