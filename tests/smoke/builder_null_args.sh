# A comptime builder given a NULL required argument, or input of the wrong
# kind, reports it at the call instead of building a wrong tree.
. "$(dirname "$0")/lib.sh"

# bad BODY MESSAGE: BODY runs inside WithFn(fn) of an int function f(int p);
# compiling must fail with MESSAGE.
n=0
bad() {
    n=$((n + 1))
    cat >"bad$n.c" <<EOF2
typedef struct { int x; } P;
typedef enum { A, B } E;
@comptime void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionAddParam(fn, "p", GetType("int"));
    Type *it = GetType("int");
    Node *one = MakeIntLiteral(1);
    Node *none = NULL;
    (void)it; (void)one; (void)none;
    WithFn(fn) { $1 }
}
gen();
int main(void) { return 0; }
EOF2
    expect_fail "$CCCC" "bad$n.c"
    grep -qF -- "$2" err.txt || {
        cat err.txt >&2
        fail "bad$n.c ($1): expected '$2'"
    }
}

bad 'MakePointer(NULL);' 'MakePointer: base type is NULL'
bad 'MakeArray(NULL, 1);' 'MakeArray: element type is NULL'
bad 'MakeArray(it, -1);' 'MakeArray: length is negative'
bad 'MakeConst(NULL);' 'MakeConst: type is NULL'
bad 'MakeVolatile(NULL);' 'MakeVolatile: type is NULL'
bad 'MakeStringLiteral(NULL);' 'MakeStringLiteral: string is NULL'
bad 'MakeParamRef(fn, "nope");' "MakeParamRef: 'f' has no parameter 'nope'"
bad 'MakeBinary(NK_ADD, none, one);' 'MakeBinary: left operand is NULL'
bad 'MakeBinary(NK_ADD, one, none);' 'MakeBinary: right operand is NULL'
bad 'MakeUnary(NK_NEG, none);' 'MakeUnary: operand is NULL'
bad 'MakeCast(none, it);' 'MakeCast: expression is NULL'
bad 'MakeCast(one, NULL);' 'MakeCast: type is NULL'
bad 'MakeIf(none, one, NULL);' 'MakeIf: condition is NULL'
bad 'MakeSwitch(none);' 'MakeSwitch: condition is NULL'
bad 'MakeLocalVar("v", NULL);' 'MakeLocalVar: type is NULL'
bad 'MakeLocalVarUnique(NULL);' 'MakeLocalVarUnique: type is NULL'
bad 'MakeAssign(none, one);' 'MakeAssign: target is NULL'
bad 'MakeAssign(one, none);' 'MakeAssign: value is NULL'
bad 'MakeMember(none, "x");' 'MakeMember: object is NULL'
bad 'MakeMember(MakeLocalVar("s", GetType("P")), "nope");' 'MakeMember: object is not a struct or union with that member'
bad 'MakeWhile(none, one);' 'MakeWhile: condition is NULL'
bad 'MakeDoWhile(one, none);' 'MakeDoWhile: condition is NULL'
bad 'MakeCond(none, one, one);' 'MakeCond: condition is NULL'
bad 'MakeCond(one, one, none);' 'MakeCond: else expression is NULL'
bad 'MakeSizeofType(NULL);' 'MakeSizeofType: type is NULL'
bad 'MakeAlignofType(NULL);' 'MakeAlignofType: type is NULL'
bad 'MakeSizeofExpr(none);' 'MakeSizeofExpr: expression is NULL'
bad 'MakeSubscript(none, one);' 'MakeSubscript: array is NULL'
bad 'MakeExprStmt(none);' 'MakeExprStmt: expression is NULL'
bad 'BlockAddStmt(MakeBlock(NULL, 0), none);' 'BlockAddStmt: statement is NULL'
bad 'BlockAddStmt(none, one);' 'BlockAddStmt: block is NULL'
bad 'FunctionSetBody(fn, none);' 'FunctionSetBody: body is NULL'
bad 'FunctionAddParam(fn, "q", NULL);' 'FunctionAddParam: type is NULL'
bad 'MakeFunction(NULL, it);' 'MakeFunction: name is NULL'
bad 'MakeFunction("g", NULL);' 'MakeFunction: return type is NULL'
bad 'FunctionPrototype("g", NULL);' 'FunctionPrototype: return type is NULL'
bad 'GlobalVar("g", NULL);' 'GlobalVar: type is NULL'
bad 'PublishNode((Obj *)NULL);' 'PublishNode: object is NULL'
bad 'MakeStruct(NULL);' 'MakeStruct: name is NULL'
bad 'MakeUnion(NULL);' 'MakeUnion: name is NULL'
bad 'MakeEnum(NULL);' 'MakeEnum: name is NULL'
bad 'MakeTypedef("T", NULL);' 'MakeTypedef: type is NULL'
bad 'StructAddField(GetType("int"), "f", it);' 'StructAddField: type is not a struct or union'
bad 'StructAddField(GetType("P"), "f", NULL);' 'StructAddField: field type is NULL'
bad 'CopyNode(none);' 'CopyNode: node is NULL'
bad 'InitArray(it);' 'InitArray: needs at least one element'
bad 'InitStruct(it, NULL, NULL, 0);' 'InitStruct: type is not a struct or union'
bad 'Serialize(GetType("P"), none, one);' 'Serialize: expression is NULL'
bad 'Deserialize(GetType("P"), none);' 'Deserialize: buffer is NULL'
bad 'EnumToString(GetType("E"), none);' 'EnumToString: expression is NULL'
bad 'EnumFromString(NULL, one);' 'EnumFromString: type is NULL'
