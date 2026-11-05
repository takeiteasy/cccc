// CCCC_FLAGS: --testing
// Suite: custom comptime attributes on function parameters.

#include <cccc/derive.h>
#include <string.h>

// A parameter handler sees its parameter, position and function type.
@comptime(attribute("track")) void track_param(AttrTarget *target, ...) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_PARAM)
        MacroErrorAt(0, "track expected a parameter target");
    Type *fn = AttrTargetOwner(target);
    int   i  = AttrTargetParamIndex(target);
    if (!fn || TypeParamAt(fn, i) != AttrTargetParam(target))
        MacroErrorAt(0, "owner and index do not lead to the parameter");
    if (AttrTargetType(target) != AttrTargetParam(target))
        MacroErrorAt(0, "target type should be the parameter's type");
    if (AttrTargetMember(target))
        MacroErrorAt(0, "a parameter target has no member");
}

// A function-level handler reads the parameter attributes.
@comptime(attribute("describe")) void describe_fn(AttrTarget *target) {
    Type *ty  = AttrTargetType(target);
    int   sum = 0, tracked = 0, uses = 0;
    for (int i = 0; i < TypeParamCount(ty); i++) {
        Type *p = TypeParamAt(ty, i);
        if (ParamHasAttr(p, "weight"))
            sum +=
                (int)strlen(StringLiteralValue(ParamAttrArg(p, "weight", 0)));
        if (ParamHasAttr(p, "track"))
            tracked++;
        uses += ParamAttrCount(p);
    }
    char fname[128];
    strcpy(fname, "describe_");
    strcat(fname, AttrTargetName(target));
    Obj *fn = MakeFunction(fname, GetType("int"));
    WithFn(fn) {
        FunctionSetBody(
            fn, MakeReturn(MakeIntLiteral(sum * 10000 + tracked * 100 + uses)));
    }
    PublishNode(fn);
}

@comptime(attribute("weight")) void weight_param(AttrTarget *target, Node *w) {
    if (!StringLiteralValue(w)[0])
        MacroErrorAt(0, "weight must be non-empty");
}

// Prefix and suffix positions, and an array parameter adjusted to a pointer.
@describe int mixed(int a @weight("xxx") @track, @track int b, int c[] @track,
                    int d) {
    return a + b + c[0] + d;
}

// An unnamed parameter in a prototype.
@describe int unnamed(int @weight("xx"), int);

[[cccc::test]]
void test_every_position_is_recorded(void) {
    int c[1] = {3};
    AssertEq(mixed(1, 2, c, 4), 10);
    // weight 3; tracked: a b c = 3; uses: a2 b1 c1 = 4
    AssertEq(describe_mixed(), 30304);
    AssertEq(describe_unnamed(), 20001);
}

// A bodyless declaration and its definition: the attributes are written once
// and the definition inherits them.
int inherited(int w @weight("xx"), int h);

@describe int inherited(int w, int h) {
    return w * h;
}

[[cccc::test]]
void test_definition_inherits_declaration(void) {
    AssertEq(inherited(6, 7), 42);
    AssertEq(describe_inherited(), 20001);
}

// A later declaration may add an attribute to another parameter.
int accumulate(int a, int b @weight("x"));
int accumulate(int a @weight("yy"), int b);

@describe int accumulate(int a, int b) {
    return a + b;
}

[[cccc::test]]
void test_declarations_accumulate_attributes(void) {
    AssertEq(accumulate(20, 22), 42);
    AssertEq(describe_accumulate(), 30002);
}

// A suffix attribute does not leak onto a sibling parameter.
@comptime(attribute("probe")) void probe_fn(AttrTarget *target) {
    Type *ty = AttrTargetType(target);
    if (!ParamHasAttr(TypeParamAt(ty, 0), "weight"))
        MacroErrorAt(0, "a lost its attribute");
    if (ParamHasAttr(TypeParamAt(ty, 1), "weight") ||
        ParamHasAttr(TypeParamAt(ty, 2), "weight"))
        MacroErrorAt(0, "attribute leaked onto a sibling parameter");
}

@probe int probed(int a @weight("xx"), int b, int c) {
    return a + b + c;
}

[[cccc::test]]
void test_attribute_stays_on_parameter(void) {
    AssertEq(probed(1, 2, 3), 6);
}

// Name-based queries find the first use; iteration sees every use.
@comptime(attribute("check_repeat")) void check_repeat(AttrTarget *target) {
    Type *p = TypeParamAt(AttrTargetType(target), 0);
    if (ParamAttrCount(p) != 3)
        MacroErrorAt(0, "expected three uses");
    if (strcmp(StringLiteralValue(ParamAttrArg(p, "weight", 0)), "first"))
        MacroErrorAt(0, "first use should win");
    if (strcmp(ParamAttrNameAt(p, 2), "weight") ||
        strcmp(StringLiteralValue(ParamAttrArgAt(p, 2, 0)), "last"))
        MacroErrorAt(0, "iteration should reach the last use");
    if (ParamAttrArgCount(p, "weight") != 1 ||
        ParamAttrArgCount(p, "nope") != -1 || ParamAttrArgCountAt(p, 9) != -1)
        MacroErrorAt(0, "arg count");
    if (ParamAttrArg(p, "weight", 5) || ParamAttrNameAt(p, 9))
        MacroErrorAt(0, "out of range should be NULL");
}

@check_repeat int repeated(int v @weight("first") @track @weight("last")) {
    return v;
}

[[cccc::test]]
void test_repeated_attribute(void) {
    AssertEq(repeated(7), 7);
}

// @default on trailing parameters, read back through reflection.
@comptime(attribute("defaults")) void defaults_fn(AttrTarget *target) {
    Type *ty = AttrTargetType(target);
    int   n  = 0;
    for (int i = 0; i < TypeParamCount(ty); i++)
        if (ParamHasAttr(TypeParamAt(ty, i), "default") &&
            ParamAttrArgCount(TypeParamAt(ty, i), "default") == 1)
            n++;
    char fname[128];
    strcpy(fname, "defaults_");
    strcat(fname, AttrTargetName(target));
    Obj *fn = MakeFunction(fname, GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeIntLiteral(n)));
    }
    PublishNode(fn);
}

int area(int w @ default(80), int h @ default(24));

@defaults int area(int w, int h) {
    return w * h;
}

@defaults int offset(int base, int by @ default(2)) {
    return base + by;
}

[[cccc::test]]
void test_default_attribute_on_parameters(void) {
    AssertEq(area(6, 7), 42);
    AssertEq(defaults_area(), 2);
    AssertEq(offset(40, 2), 42);
    AssertEq(defaults_offset(), 1);
}
