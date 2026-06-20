// Test ticket #271: unified PublishNode API.

[[cccc::comptime]]
void publish_proto(void) {
    Type *int_ty = GetType("int");
    Obj *proto = FunctionPrototype("published_add_one", int_ty);
    FunctionAddParam(proto, "x", int_ty);
    PublishNode(proto);
}
publish_proto();

[[cccc::comptime]]
void define_published_fn(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("published_add_one", int_ty);
    FunctionSetBody(fn,
        MakeReturn(MakeBinary(NK_ADD, MakeParamRef(fn, "x"),
                                MakeIntLiteral(1))));
}
define_published_fn();

[[cccc::comptime]]
void publish_global_and_types(void) {
    Type *char_ty = GetType("char");
    Type *arr_ty = MakeArray(char_ty, 4);
    Obj *g = GlobalVar("published_bytes", arr_ty);
    GlobalVarSetInitData(g, "CCCC\0", 4);
    PublishNodeAt(g, SyntheticToken("published global"));

    Type *int_ty = GetType("int");
    Type *point = MakeStruct("PublishedPoint");
    StructAddField(point, "x", int_ty);
    StructAddField(point, "y", int_ty);
    PublishNode(point);

    Type *tagged = MakeEnum("PublishedTag");
    EnumAddConstant(tagged, "PUBLISHED_TAG_OK", 7);
    PublishNode(tagged);

    PublishNode(MakeTypedef("PublishedLong", GetType("long")));
}
publish_global_and_types();

[[cccc::comptime]]
void forward_alias_still_works(void) {
    Type *int_ty = GetType("int");
    Obj *proto = FunctionPrototype("published_alias_fn", int_ty);
    PublishNode(proto);
}
forward_alias_still_works();

[[cccc::comptime]]
void define_alias_fn(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("published_alias_fn", int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(5)));
}
define_alias_fn();

int main(void) {
    if (published_add_one(41) != 42) return 1;
    if (published_bytes[0] != 'C') return 2;
    if (published_bytes[1] != 'C') return 3;
    if (published_bytes[2] != 'C') return 4;

    struct PublishedPoint p;
    p.x = 20;
    p.y = 22;
    if (p.x + p.y != 42) return 5;

    PublishedLong n = 42;
    if (n != 42) return 6;
    if (PUBLISHED_TAG_OK != 7) return 7;
    if (published_alias_fn() != 5) return 8;
    return 42;
}
