// Ticket #235: Serialize/Deserialize round-trip for a flat struct with a
// nested struct field.

#include <string.h>

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    struct Inner inner;
    double y;
};

[[cccc::comptime]]
void generate_outer_serdes(void) {
    Type *ty = GetType("Outer");

    Obj *pack = MakeFunction("outer_pack", GetType("int"));
    FunctionAddParam(pack, "self", MakePointer(ty));
    FunctionAddParam(pack, "buf", MakePointer(GetType("void")));
    WithFn(pack) {
        Node *self = MakeUnary(NK_DEREF, MakeParamRef(pack, "self"));
        Node *buf = MakeParamRef(pack, "buf");
        Node *block = Serialize(ty, self, buf);
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(TypeSize(ty))));
        FunctionSetBody(pack, block);
    }

    Obj *unpack = MakeFunction("outer_unpack", ty);
    FunctionAddParam(unpack, "buf", MakePointer(GetType("void")));
    WithFn(unpack) {
        FunctionSetBody(unpack, MakeReturn(Deserialize(ty, MakeParamRef(unpack, "buf"))));
    }
}

generate_outer_serdes();

int main(void) {
    struct Outer o = {1, {2, 3}, 4.5};
    char buf[sizeof(struct Outer)];

    // Serialize writes each member individually and never touches struct
    // padding, so the deserialized copy's padding bytes would otherwise be
    // indeterminate. Zero buf up front so the final memcmp (which spans
    // padding) compares like with like.
    memset(buf, 0, sizeof buf);

    int n = outer_pack(&o, buf);
    if (n != (int)sizeof(struct Outer))
        return 1;

    struct Outer o2 = outer_unpack(buf);
    if (memcmp(&o, &o2, sizeof(struct Outer)) != 0)
        return 2;

    return 42;
}
