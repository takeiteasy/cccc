// A declaration has no body to walk; accessors given the wrong kind of input
// answer NULL or 0 rather than failing.

int proto(int a, int b);
int a_global = 5;

int block_count_of(const char *name);

[[cccc::comptime]] Node *probe(void) {
    Obj *decl = FindGlobal("proto");
    Obj *var  = FindGlobal("a_global");
    if (!decl || !var)
        return MakeIntLiteral(1);
    if (FnBody(decl) != 0 || FnParamCount(decl) != 0 || FnLocalCount(decl) != 0)
        return MakeIntLiteral(2);
    if (FnBody(var) != 0 || FnParamAt(var, 0) != 0 || FnLocalAt(var, 0) != 0)
        return MakeIntLiteral(3);
    if (FnBody(0) != 0 || FnParamCount(0) != 0 || FnLocalCount(0) != 0)
        return MakeIntLiteral(4);
    if (GetNodeKind(0) != NK_NULL_EXPR || GetNodeType(0) != 0 ||
        NodeNext(0) != 0 || NodeLhs(0) != 0 || BlockCount(0) != 0 ||
        BlockAt(0, 0) != 0 || CallArgCount(0) != 0 || NodeIntValue(0) != 0 ||
        NodeVarObj(0) != 0 || SwitchCaseCount(0) != 0 || ObjNext(0) != 0)
        return MakeIntLiteral(5);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
