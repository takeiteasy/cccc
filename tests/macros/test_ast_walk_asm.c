// An attribute handler reads the template, operands, clobbers and flags of
// inline asm statements.

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

    Node *a = nth(seen, nseen, NK_ASM, 0), *b = nth(seen, nseen, NK_ASM, 1),
         *c = nth(seen, nseen, NK_ASM, 2);
    expect(__LINE__, a && b && c && nth(seen, nseen, NK_ASM, 3) == 0);

    // asm volatile("nop" : "=r"(*out), [acc] "+r"(io) : "r"(in), [imm] "i"(7)
    //              : "memory", "cc");
    expect(__LINE__, AsmTemplate(a)[0] == 'n' && AsmTemplate(a)[2] == 'p');
    expect(__LINE__, AsmIsVolatile(a) && !AsmIsGoto(a));
    expect(__LINE__, AsmOutputCount(a) == 2 && AsmInputCount(a) == 2);
    expect(__LINE__, AsmOperandConstraint(a, 0)[0] == '=');
    expect(__LINE__, AsmOperandConstraint(a, 1)[0] == '+');
    expect(__LINE__, AsmOperandConstraint(a, 2)[0] == 'r');
    expect(__LINE__, AsmOperandConstraint(a, 3)[0] == 'i');
    expect(__LINE__, AsmOperandName(a, 0) == 0);
    expect(__LINE__, AsmOperandName(a, 1)[0] == 'a');
    expect(__LINE__, AsmOperandName(a, 2) == 0);
    expect(__LINE__, AsmOperandName(a, 3)[0] == 'i');
    expect(__LINE__, GetNodeKind(AsmOperandExpr(a, 0)) == NK_DEREF);
    expect(__LINE__,
           GetNodeKind(StripImplicit(AsmOperandExpr(a, 1))) == NK_VAR);
    expect(__LINE__, NodeIntValue(StripImplicit(AsmOperandExpr(a, 3))) == 7);
    expect(__LINE__, AsmClobberCount(a) == 2);
    expect(__LINE__, AsmClobberAt(a, 0)[0] == 'm');
    expect(__LINE__, AsmClobberAt(a, 1)[0] == 'c');
    expect(__LINE__, AsmClobberAt(a, 2) == 0 && AsmClobberAt(a, -1) == 0);
    expect(__LINE__, AsmOperandConstraint(a, 4) == 0);
    expect(__LINE__, AsmOperandExpr(a, 4) == 0);
    expect(__LINE__, AsmGotoLabelCount(a) == 0);

    // asm("nop"): basic, not volatile.
    expect(__LINE__, !AsmIsVolatile(b) && !AsmIsGoto(b));
    expect(__LINE__, AsmOutputCount(b) == 0 && AsmInputCount(b) == 0);
    expect(__LINE__, AsmClobberCount(b) == 0);

    // asm goto("nop" : : : : one, two)
    expect(__LINE__, AsmIsGoto(c) && !AsmIsVolatile(c));
    expect(__LINE__, AsmGotoLabelCount(c) == 2);
    expect(__LINE__, AsmGotoLabelAt(c, 0)[0] == 'o');
    expect(__LINE__, AsmGotoLabelAt(c, 1)[0] == 't');
    expect(__LINE__, AsmGotoLabelAt(c, 2) == 0);

    // Other kinds and NULL.
    expect(__LINE__, AsmTemplate(0) == 0 && AsmTemplate(seen[0]) == 0);
    expect(__LINE__, AsmOutputCount(seen[0]) == 0);
    expect(__LINE__, AsmInputCount(0) == 0 && AsmClobberCount(0) == 0);
    expect(__LINE__, !AsmIsVolatile(seen[0]) && !AsmIsGoto(0));
    expect(__LINE__, AsmGotoLabelCount(seen[0]) == 0);

    Obj *r = MakeFunction("walk_result", GetType("int"));
    WithFn(r) {
        FunctionSetBody(r, MakeReturn(MakeIntLiteral(42)));
    }
    PublishNode(r);
}

@inspect void asms(int *out, int in) {
    int io = 0;
    asm volatile("nop"
                 : "=r"(*out), [acc] "+r"(io)
                 : "r"(in), [imm] "i"(7)
                 : "memory", "cc");
    asm("nop");
    asm goto("nop" : : : : one, two);
one:
    return;
two:
    return;
}

int walk_result(void);

int main(void) {
    return walk_result();
}
