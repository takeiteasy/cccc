// RegisterMethod / RegisterStaticMethod bind generated functions to a type.
struct Tag {
    int a;
};

[[cccc::comptime]]
void gen_methods(void) {
    Type *ty = GetType("Tag");

    Obj *get = MakeFunction("tag_get", GetType("int"));
    FunctionAddParam(get, "self", MakePointer(ty));
    WithFn(get) {
        Node *self = MakeUnary(NK_DEREF, MakeParamRef(get, "self"));
        FunctionSetBody(get, MakeReturn(MakeMember(self, "a")));
    }
    PublishNode(get);
    RegisterMethod(get, ty, "get");

    Obj *make = MakeFunction("tag_make", ty);
    WithFn(make) {
        FunctionSetBody(make, Quote("return (struct Tag){40};"));
    }
    PublishNode(make);
    RegisterStaticMethod(make, ty, "make");
}
gen_methods();

int main(void) {
    struct Tag t = struct Tag.make();
    return t.get() + 2;
}
