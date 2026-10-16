// CCCC_FLAGS: --testing
// Suite: custom comptime attributes on struct members.

#include <string.h>

int member_log[32];
int member_log_n;

// A member handler sees its member, owner and final layout.
@comptime(attribute("track")) void track_member(AttrTarget *target, ...) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(0, "track expected a member target");
    Member *m = AttrTargetMember(target);
    if (strcmp(AttrTargetName(target), MemberName(m)))
        MacroErrorAt(0, "target name differs from member name");
    if (!AttrTargetOwner(target))
        MacroErrorAt(0, "member target has no owner");
}

// A struct-level handler publishes a function reading member attributes.
@comptime(attribute("describe")) void describe_struct(AttrTarget *target) {
    Type *ty  = AttrTargetType(target);
    int   sum = 0, tracked = 0, uses = 0;
    for (int i = 0; i < StructMemberCount(ty); i++) {
        Member *m = StructMemberAt(ty, i);
        if (MemberHasAttr(m, "weight"))
            sum +=
                (int)strlen(StringLiteralValue(MemberAttrArg(m, "weight", 0)));
        if (MemberHasAttr(m, "track"))
            tracked++;
        uses += MemberAttrCount(m);
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

@comptime(attribute("weight")) void weight_member(AttrTarget *target, Node *w) {
    if (!StringLiteralValue(w)[0])
        MacroErrorAt(0, "weight must be non-empty");
}

@describe struct Positions {
    int        a @track;
    int        b @weight("xxx") @track, c @weight("xxxx");
    @track int d, e;
    int        f : 3 @track;
};

[[cccc::test]]
void test_every_position_is_recorded(void) {
    // weights 3 + 4 = 7; tracked: a b d e f = 5; uses: a1 b2 c1 d1 e1 f1 = 7
    AssertEq(describe_Positions(), 70507);
}

@describe struct Outer {
    struct Inner {
        int x @track;
    } in;
    int y @track;
};

@describe typedef struct {
    int z @weight("x");
} Typedefed;

@describe struct Plain {
    int unmarked;
};

[[cccc::test]]
void test_nested_typedef_and_plain(void) {
    AssertEq(describe_Outer(), 100 + 1);
    AssertEq(describe_Typedefed(), 10000 + 1);
    AssertEq(describe_Plain(), 0);
}

// A suffix attribute does not leak to a sibling or to later uses of the type.
@comptime(attribute("probe")) void probe_attr(AttrTarget *target) {
    Type *ty = AttrTargetType(target);
    if (!MemberHasAttr(StructMemberFind(ty, "marked"), "weight"))
        MacroErrorAt(0, "marked lost its attribute");
    if (MemberHasAttr(StructMemberFind(ty, "unmarked"), "weight") ||
        MemberHasAttr(StructMemberFind(ty, "later"), "weight"))
        MacroErrorAt(0, "attribute leaked onto a sibling member");
}

@probe struct Probed {
    int marked @weight("xx"), unmarked;
    int later;
};

[[cccc::test]]
void test_attribute_stays_on_member(void) {
    struct Probed p = {1, 2, 3};
    AssertEq(p.marked + p.unmarked + p.later, 6);
}

// Name-based queries find the first use; iteration sees every use.
@comptime(attribute("check_repeat")) void check_repeat(AttrTarget *target) {
    Member *m = StructMemberFind(AttrTargetType(target), "v");
    if (MemberAttrCount(m) != 3)
        MacroErrorAt(0, "expected three uses");
    if (strcmp(StringLiteralValue(MemberAttrArg(m, "weight", 0)), "first"))
        MacroErrorAt(0, "first use should win");
    if (strcmp(MemberAttrNameAt(m, 2), "weight") ||
        strcmp(StringLiteralValue(MemberAttrArgAt(m, 2, 0)), "last"))
        MacroErrorAt(0, "iteration should reach the last use");
    if (MemberAttrArgCount(m, "weight") != 1 ||
        MemberAttrArgCount(m, "nope") != -1)
        MacroErrorAt(0, "arg count");
    if (MemberAttrArg(m, "weight", 5) || MemberAttrNameAt(m, 9))
        MacroErrorAt(0, "out of range should be NULL");
}

@check_repeat struct Repeated {
    int v @weight("first") @track @weight("last");
};

[[cccc::test]]
void test_repeated_attribute(void) {
    AssertEq((int)sizeof(struct Repeated), 4);
}
