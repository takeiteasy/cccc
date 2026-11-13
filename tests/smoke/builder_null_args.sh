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
@comptime int cv = 1;
@comptime struct { int w; } cs = { 2 };
@template(T) T twice(T a) { return a + a; }
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

bad 'SwitchAddCase(none, one, one);' 'SwitchAddCase: switch is NULL'
bad 'SwitchAddCase(MakeSwitch(one), none, one);' 'SwitchAddCase: value is NULL'
bad 'SwitchAddCase(MakeSwitch(one), one, none);' 'SwitchAddCase: body is NULL'
bad 'SwitchAddCase(one, one, one);' 'SwitchAddCase: node is not a switch'
bad 'SwitchAddCase(one, one);' 'SwitchAddCase: no WithSwitch context'
bad 'SwitchSetDefault(none, one);' 'SwitchSetDefault: switch is NULL'
bad 'SwitchSetDefault(MakeSwitch(one), none);' 'SwitchSetDefault: body is NULL'
bad 'SwitchSetDefault(one, one);' 'SwitchSetDefault: node is not a switch'
bad 'SwitchSetDefault(one);' 'SwitchSetDefault: no WithSwitch context'
bad 'FunctionSetStatic(NULL, 1);' 'FunctionSetStatic: function is NULL'
bad 'FunctionSetInline(NULL, 1);' 'FunctionSetInline: function is NULL'
bad 'FunctionSetVariadic(NULL, 1);' 'FunctionSetVariadic: function is NULL'
bad 'AddAttribute(NULL, "noreturn");' 'AddAttribute: function is NULL'
bad 'AddAttribute(fn, NULL);' 'AddAttribute: attribute text is NULL'
bad 'GlobalVarSetStatic(NULL, 1);' 'GlobalVarSetStatic: variable is NULL'
bad 'GlobalVarSetInitData(NULL, "ab", 2);' 'GlobalVarSetInitData: variable is NULL'
bad 'GlobalVarSetInitData(GlobalVar("gv", it), NULL, 2);' 'GlobalVarSetInitData: data is NULL'
bad 'GlobalVarSetInitData(GlobalVar("gv", it), "ab", 0);' 'GlobalVarSetInitData: length is not positive'
bad 'EnumAddConstant(NULL, "X", 1);' 'EnumAddConstant: type is NULL'
bad 'EnumAddConstant(GetType("E"), NULL, 1);' 'EnumAddConstant: name is NULL'
bad 'EnumAddConstant(it, "X", 1);' 'EnumAddConstant: type is not an enum'
bad 'EnumAddConstant("X", 1);' 'EnumAddConstant: no WithEnum context'
bad 'WithFn(NULL) { }' 'WithFn: function is NULL'
bad 'WithLoop(none) { }' 'WithLoop: loop is NULL'
bad 'WithBlock(none) { }' 'WithBlock: block is NULL'
bad 'WithSwitch(none) { }' 'WithSwitch: switch is NULL'
bad 'WithStruct(NULL) { }' 'WithStruct: struct type is NULL'
bad 'WithEnum(NULL) { }' 'WithEnum: enum type is NULL'
bad 'BlockAddStmt(one);' 'BlockAddStmt: no WithBlock context'
bad 'StructAddField("f", it);' 'StructAddField: no WithStruct context'
bad 'Quote(NULL);' 'Quote: template is NULL'
bad 'QuoteLazy(NULL);' 'QuoteLazy: template is NULL'
bad 'QuoteN(NULL, NULL, 0);' 'QuoteN: template is NULL'
bad 'QuoteN("$1", NULL, 1);' 'QuoteN: nodes is NULL'
bad 'QuoteLazyN(NULL, NULL, 0);' 'QuoteLazyN: template is NULL'
bad 'QuoteLazyN("$1", NULL, 1);' 'QuoteLazyN: nodes is NULL'
bad 'GetComptimeInt("nope");' "GetComptimeInt: no comptime variable 'nope'"
bad 'GetComptimeInt(NULL);' 'GetComptimeInt: name is NULL'
bad 'GetComptimeInt("cs");' "GetComptimeInt: 'cs' is a struct; use GetComptimeMember"
bad 'GetComptimeFloat("nope");' "GetComptimeFloat: no comptime variable 'nope'"
bad 'GetComptimeVar("nope");' "GetComptimeVar: no comptime variable 'nope'"
bad 'GetComptimePtr("nope");' "GetComptimePtr: no comptime variable 'nope'"
bad 'GetComptimeMember("cs", "zz");' "GetComptimeMember: 'cs' has no member 'zz'"
bad 'GetComptimeMember("cv", "w");' "GetComptimeMember: 'cv' is not a struct; use GetComptimeVar"
bad 'GetConstexprValue("nope");' "GetConstexprValue: 'nope' is not a constexpr global"
bad 'InstantiateType(NULL, "int");' 'InstantiateType: name is NULL'
bad 'InstantiateFunction(NULL, "int");' 'InstantiateFunction: name is NULL'
bad 'InstantiateTypeWith(NULL, NULL, 0);' 'InstantiateTypeWith: name is NULL'
bad 'InstantiateFunctionWith(NULL, NULL, 0);' 'InstantiateFunctionWith: name is NULL'
